/**
 * Glif N-API addon — wraps the C rendering pipeline for Node.js.
 *
 * Exposes:
 *   render(imagePath, fontPath, cellW, cellH, dirCrunch, globalCrunch)
 *   Low-level wrapper classes: Image, LightnessMap, SamplingConfig,
 *     PrecomputedMasks, CharDatabase, Grid
 */

#include <napi.h>

extern "C" {
#include "image.h"
#include "sampling.h"
#include "grid.h"
#include "font.h"
#include "contrast.h"
#include "match.h"
}

// ── High-level render ──────────────────────────────────────────────────────

static Napi::Value Render(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 6) {
        Napi::TypeError::New(env, "Expected 6 arguments: imagePath, fontPath, cellW, cellH, dirCrunch, globalCrunch")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string imagePath = info[0].As<Napi::String>().Utf8Value();
    std::string fontPath = info[1].As<Napi::String>().Utf8Value();
    int cellW = info[2].As<Napi::Number>().Int32Value();
    int cellH = info[3].As<Napi::Number>().Int32Value();
    float dirCrunch = info[4].As<Napi::Number>().FloatValue();
    float globalCrunch = info[5].As<Napi::Number>().FloatValue();

    // 1. Load image
    GlifImage img;
    if (glif_image_load(&img, imagePath.c_str()) != 0) {
        Napi::Error::New(env, "Failed to load image: " + imagePath)
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // 2. Lightness map
    GlifLightnessMap lm = { .width = 0, .height = 0, .data = nullptr };
    if (glif_lightness_map_create(&lm, &img) != 0) {
        glif_image_free(&img);
        Napi::Error::New(env, "Failed to create lightness map")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // 3. Sampling geometry
    GlifSamplingConfig sc;
    glif_sampling_config_init(&sc);

    GlifPrecomputedMasks pm;
    if (glif_sampling_precompute(&pm, &sc, cellW, cellH, lm.width) != 0) {
        glif_lightness_map_free(&lm);
        glif_image_free(&img);
        Napi::Error::New(env, "Failed to precompute sampling masks")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // 4. Font
    GlifCharDatabase db;
    if (glif_char_db_create(&db, fontPath.c_str(), cellW, cellH, &sc) != 0) {
        glif_sampling_precompute_free(&pm);
        glif_lightness_map_free(&lm);
        glif_image_free(&img);
        Napi::Error::New(env, "Failed to load font: " + fontPath)
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // 5. Grid
    GlifGrid grid;
    if (glif_grid_create(&grid, &img, cellW, cellH) != 0) {
        glif_char_db_free(&db);
        glif_sampling_precompute_free(&pm);
        glif_lightness_map_free(&lm);
        glif_image_free(&img);
        Napi::Error::New(env, "Image too small for cell size")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // 6-10. Pipeline
    glif_grid_compute_vectors_fast(&grid, &lm, &pm);
    glif_grid_compute_colors(&grid, &img);

    GlifAdaptiveContrast ac = { .floor = -1.0f, .ceil = 0.0f,
                            .frame_avg = 0.0f, .frame_p10 = 0.0f, .frame_p90 = 0.0f };
    glif_contrast_analyze_frame(&ac, &grid);
    glif_contrast_directional(&grid, &sc, dirCrunch, &ac);
    glif_contrast_global(&grid, globalCrunch, &ac);
    glif_match_grid(&grid, &db);

    // Extract results
    int rows = grid.rows;
    int cols = grid.cols;
    int total = rows * cols;

    Napi::Array cells = Napi::Array::New(env, total);
    for (int i = 0; i < total; i++) {
        GlifGridCell* c = &grid.cells[i];
        Napi::Object cell = Napi::Object::New(env);
        char chStr[2] = { c->ch, '\0' };
        cell.Set("ch", Napi::String::New(env, chStr));
        cell.Set("r", Napi::Number::New(env, c->r));
        cell.Set("g", Napi::Number::New(env, c->g));
        cell.Set("b", Napi::Number::New(env, c->b));
        cells.Set(static_cast<uint32_t>(i), cell);
    }

    // Cleanup
    glif_grid_free(&grid);
    glif_char_db_free(&db);
    glif_sampling_precompute_free(&pm);
    glif_lightness_map_free(&lm);
    glif_image_free(&img);

    Napi::Object result = Napi::Object::New(env);
    result.Set("rows", Napi::Number::New(env, rows));
    result.Set("cols", Napi::Number::New(env, cols));
    result.Set("cells", cells);
    return result;
}

// ── Low-level wrapper: Image ────────────────────────────────────────────────

class ImageWrap : public Napi::ObjectWrap<ImageWrap> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "Image", {
            InstanceMethod("free", &ImageWrap::Free),
            InstanceAccessor("width", &ImageWrap::GetWidth, nullptr),
            InstanceAccessor("height", &ImageWrap::GetHeight, nullptr),
            InstanceAccessor("channels", &ImageWrap::GetChannels, nullptr),
        });
        Napi::FunctionReference* constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        env.SetInstanceData(constructor);
        exports.Set("Image", func);
        return exports;
    }

    ImageWrap(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<ImageWrap>(info), freed_(false) {
        Napi::Env env = info.Env();
        std::string path = info[0].As<Napi::String>().Utf8Value();
        if (glif_image_load(&img_, path.c_str()) != 0) {
            Napi::Error::New(env, "Failed to load image: " + path)
                .ThrowAsJavaScriptException();
        }
    }

    ~ImageWrap() { DoFree(); }
    GlifImage* ptr() { return &img_; }

private:
    void DoFree() {
        if (!freed_) { glif_image_free(&img_); freed_ = true; }
    }
    Napi::Value Free(const Napi::CallbackInfo& info) {
        DoFree();
        return info.Env().Undefined();
    }
    Napi::Value GetWidth(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), img_.width);
    }
    Napi::Value GetHeight(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), img_.height);
    }
    Napi::Value GetChannels(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), img_.channels);
    }

    GlifImage img_;
    bool freed_;
};

// ── Low-level wrapper: LightnessMap ─────────────────────────────────────────

class LightnessMapWrap : public Napi::ObjectWrap<LightnessMapWrap> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "LightnessMap", {
            InstanceMethod("free", &LightnessMapWrap::Free),
            InstanceMethod("normalize", &LightnessMapWrap::Normalize),
            InstanceAccessor("width", &LightnessMapWrap::GetWidth, nullptr),
            InstanceAccessor("height", &LightnessMapWrap::GetHeight, nullptr),
        });
        exports.Set("LightnessMap", func);
        return exports;
    }

    LightnessMapWrap(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<LightnessMapWrap>(info), freed_(false) {
        Napi::Env env = info.Env();
        lm_.data = nullptr;
        ImageWrap* imgWrap = Napi::ObjectWrap<ImageWrap>::Unwrap(
            info[0].As<Napi::Object>());
        if (glif_lightness_map_create(&lm_, imgWrap->ptr()) != 0) {
            Napi::Error::New(env, "Failed to create lightness map")
                .ThrowAsJavaScriptException();
        }
    }

    ~LightnessMapWrap() { DoFree(); }
    GlifLightnessMap* ptr() { return &lm_; }

private:
    void DoFree() {
        if (!freed_) { glif_lightness_map_free(&lm_); freed_ = true; }
    }
    Napi::Value Free(const Napi::CallbackInfo& info) {
        DoFree();
        return info.Env().Undefined();
    }
    Napi::Value Normalize(const Napi::CallbackInfo& info) {
        glif_lightness_map_normalize(&lm_);
        return info.Env().Undefined();
    }
    Napi::Value GetWidth(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), lm_.width);
    }
    Napi::Value GetHeight(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), lm_.height);
    }

    GlifLightnessMap lm_;
    bool freed_;
};

// ── Low-level wrapper: SamplingConfig ───────────────────────────────────────

class SamplingConfigWrap : public Napi::ObjectWrap<SamplingConfigWrap> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "SamplingConfig", {});
        exports.Set("SamplingConfig", func);
        return exports;
    }

    SamplingConfigWrap(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<SamplingConfigWrap>(info) {
        glif_sampling_config_init(&sc_);
    }

    GlifSamplingConfig* ptr() { return &sc_; }

private:
    GlifSamplingConfig sc_;
};

// ── Low-level wrapper: PrecomputedMasks ─────────────────────────────────────

class PrecomputedMasksWrap : public Napi::ObjectWrap<PrecomputedMasksWrap> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "PrecomputedMasks", {
            InstanceMethod("free", &PrecomputedMasksWrap::Free),
        });
        exports.Set("PrecomputedMasks", func);
        return exports;
    }

    PrecomputedMasksWrap(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<PrecomputedMasksWrap>(info), freed_(false) {
        Napi::Env env = info.Env();
        SamplingConfigWrap* scWrap = Napi::ObjectWrap<SamplingConfigWrap>::Unwrap(
            info[0].As<Napi::Object>());
        int cellW = info[1].As<Napi::Number>().Int32Value();
        int cellH = info[2].As<Napi::Number>().Int32Value();
        int stride = info[3].As<Napi::Number>().Int32Value();
        if (glif_sampling_precompute(&pm_, scWrap->ptr(), cellW, cellH, stride) != 0) {
            Napi::Error::New(env, "Failed to precompute sampling masks")
                .ThrowAsJavaScriptException();
        }
    }

    ~PrecomputedMasksWrap() { DoFree(); }
    GlifPrecomputedMasks* ptr() { return &pm_; }

private:
    void DoFree() {
        if (!freed_) { glif_sampling_precompute_free(&pm_); freed_ = true; }
    }
    Napi::Value Free(const Napi::CallbackInfo& info) {
        DoFree();
        return info.Env().Undefined();
    }

    GlifPrecomputedMasks pm_;
    bool freed_;
};

// ── Low-level wrapper: CharDatabase ─────────────────────────────────────────

class CharDatabaseWrap : public Napi::ObjectWrap<CharDatabaseWrap> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "CharDatabase", {
            InstanceMethod("free", &CharDatabaseWrap::Free),
        });
        exports.Set("CharDatabase", func);
        return exports;
    }

    CharDatabaseWrap(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<CharDatabaseWrap>(info), freed_(false) {
        Napi::Env env = info.Env();
        std::string fontPath = info[0].As<Napi::String>().Utf8Value();
        int cellW = info[1].As<Napi::Number>().Int32Value();
        int cellH = info[2].As<Napi::Number>().Int32Value();
        SamplingConfigWrap* scWrap = Napi::ObjectWrap<SamplingConfigWrap>::Unwrap(
            info[3].As<Napi::Object>());
        if (glif_char_db_create(&db_, fontPath.c_str(), cellW, cellH, scWrap->ptr()) != 0) {
            Napi::Error::New(env, "Failed to load font: " + fontPath)
                .ThrowAsJavaScriptException();
        }
    }

    ~CharDatabaseWrap() { DoFree(); }
    GlifCharDatabase* ptr() { return &db_; }

private:
    void DoFree() {
        if (!freed_) { glif_char_db_free(&db_); freed_ = true; }
    }
    Napi::Value Free(const Napi::CallbackInfo& info) {
        DoFree();
        return info.Env().Undefined();
    }

    GlifCharDatabase db_;
    bool freed_;
};

// ── Low-level wrapper: Grid ─────────────────────────────────────────────────

class GridWrap : public Napi::ObjectWrap<GridWrap> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "Grid", {
            InstanceMethod("free", &GridWrap::Free),
            InstanceMethod("computeVectorsFast", &GridWrap::ComputeVectorsFast),
            InstanceMethod("computeColors", &GridWrap::ComputeColors),
            InstanceMethod("applyDirectionalContrast", &GridWrap::ApplyDirectionalContrast),
            InstanceMethod("applyGlobalContrast", &GridWrap::ApplyGlobalContrast),
            InstanceMethod("match", &GridWrap::Match),
            InstanceMethod("getCells", &GridWrap::GetCells),
            InstanceAccessor("rows", &GridWrap::GetRows, nullptr),
            InstanceAccessor("cols", &GridWrap::GetCols, nullptr),
        });
        exports.Set("Grid", func);
        return exports;
    }

    GridWrap(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<GridWrap>(info), freed_(false) {
        Napi::Env env = info.Env();
        ImageWrap* imgWrap = Napi::ObjectWrap<ImageWrap>::Unwrap(
            info[0].As<Napi::Object>());
        int cellW = info[1].As<Napi::Number>().Int32Value();
        int cellH = info[2].As<Napi::Number>().Int32Value();
        if (glif_grid_create(&grid_, imgWrap->ptr(), cellW, cellH) != 0) {
            Napi::Error::New(env, "Image too small for cell size")
                .ThrowAsJavaScriptException();
        }
    }

    ~GridWrap() { DoFree(); }

private:
    void DoFree() {
        if (!freed_) { glif_grid_free(&grid_); freed_ = true; }
    }
    Napi::Value Free(const Napi::CallbackInfo& info) {
        DoFree();
        return info.Env().Undefined();
    }
    Napi::Value GetRows(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), grid_.rows);
    }
    Napi::Value GetCols(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), grid_.cols);
    }
    Napi::Value ComputeVectorsFast(const Napi::CallbackInfo& info) {
        LightnessMapWrap* lmWrap = Napi::ObjectWrap<LightnessMapWrap>::Unwrap(
            info[0].As<Napi::Object>());
        PrecomputedMasksWrap* pmWrap = Napi::ObjectWrap<PrecomputedMasksWrap>::Unwrap(
            info[1].As<Napi::Object>());
        glif_grid_compute_vectors_fast(&grid_, lmWrap->ptr(), pmWrap->ptr());
        return info.Env().Undefined();
    }
    Napi::Value ComputeColors(const Napi::CallbackInfo& info) {
        ImageWrap* imgWrap = Napi::ObjectWrap<ImageWrap>::Unwrap(
            info[0].As<Napi::Object>());
        glif_grid_compute_colors(&grid_, imgWrap->ptr());
        return info.Env().Undefined();
    }
    Napi::Value ApplyDirectionalContrast(const Napi::CallbackInfo& info) {
        SamplingConfigWrap* scWrap = Napi::ObjectWrap<SamplingConfigWrap>::Unwrap(
            info[0].As<Napi::Object>());
        float crunch = info[1].As<Napi::Number>().FloatValue();
        GlifAdaptiveContrast ac = { .floor = -1.0f, .ceil = 0.0f,
                                .frame_avg = 0.0f, .frame_p10 = 0.0f, .frame_p90 = 0.0f };
        glif_contrast_analyze_frame(&ac, &grid_);
        glif_contrast_directional(&grid_, scWrap->ptr(), crunch, &ac);
        return info.Env().Undefined();
    }
    Napi::Value ApplyGlobalContrast(const Napi::CallbackInfo& info) {
        float crunch = info[0].As<Napi::Number>().FloatValue();
        GlifAdaptiveContrast ac = { .floor = -1.0f, .ceil = 0.0f,
                                .frame_avg = 0.0f, .frame_p10 = 0.0f, .frame_p90 = 0.0f };
        glif_contrast_analyze_frame(&ac, &grid_);
        glif_contrast_global(&grid_, crunch, &ac);
        return info.Env().Undefined();
    }
    Napi::Value Match(const Napi::CallbackInfo& info) {
        CharDatabaseWrap* dbWrap = Napi::ObjectWrap<CharDatabaseWrap>::Unwrap(
            info[0].As<Napi::Object>());
        glif_match_grid(&grid_, dbWrap->ptr());
        return info.Env().Undefined();
    }
    Napi::Value GetCells(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        int total = grid_.rows * grid_.cols;
        Napi::Array cells = Napi::Array::New(env, total);
        for (int i = 0; i < total; i++) {
            GlifGridCell* c = &grid_.cells[i];
            Napi::Object cell = Napi::Object::New(env);
            char chStr[2] = { c->ch, '\0' };
            cell.Set("ch", Napi::String::New(env, chStr));
            cell.Set("r", Napi::Number::New(env, c->r));
            cell.Set("g", Napi::Number::New(env, c->g));
            cell.Set("b", Napi::Number::New(env, c->b));
            cells.Set(static_cast<uint32_t>(i), cell);
        }
        return cells;
    }

    GlifGrid grid_;
    bool freed_;
};

// ── Module init ────────────────────────────────────────────────────────────

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("render", Napi::Function::New(env, Render));
    ImageWrap::Init(env, exports);
    LightnessMapWrap::Init(env, exports);
    SamplingConfigWrap::Init(env, exports);
    PrecomputedMasksWrap::Init(env, exports);
    CharDatabaseWrap::Init(env, exports);
    GridWrap::Init(env, exports);
    return exports;
}

NODE_API_MODULE(glif_native, Init)
