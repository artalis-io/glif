/**
 * GlifWebGLRenderer — GPU-accelerated font-atlas renderer for the Glif web demo.
 *
 * Replaces per-cell fillText() with a single fullscreen-quad draw call.
 * The fragment shader looks up character index and color from data textures,
 * then samples the font atlas to composite each glyph.
 */
class GlifWebGLRenderer {
    constructor(canvas) {
        this.canvas = canvas;
        this.gl = canvas.getContext('webgl', { antialias: false, alpha: false });
        if (!this.gl) throw new Error('WebGL not available');

        this._program = null;
        this._charTex = null;
        this._colorTex = null;
        this._atlasTex = null;
        this._quadBuf = null;
        this._gridCols = 0;
        this._gridRows = 0;
        this._charW = 0;
        this._charH = 0;
        this._physCharW = 0;
        this._physCharH = 0;
        this._atlasGridX = 16;
        this._atlasGridY = 6;
    }

    /**
     * Initialize shaders and upload font atlas texture.
     * @param {HTMLCanvasElement} atlasCanvas — offscreen canvas with glyph grid
     * @param {number} charW — logical width of each glyph cell (CSS px)
     * @param {number} charH — logical height of each glyph cell (CSS px)
     * @param {number} [physCharW] — physical width of each glyph cell (atlas px)
     * @param {number} [physCharH] — physical height of each glyph cell (atlas px)
     */
    init(atlasCanvas, charW, charH, physCharW, physCharH) {
        const gl = this.gl;
        this._charW = charW;
        this._charH = charH;
        this._physCharW = physCharW || charW;
        this._physCharH = physCharH || charH;

        // Compile shaders
        const vs = this._compile(gl.VERTEX_SHADER, GlifWebGLRenderer.VERT_SRC);
        const fs = this._compile(gl.FRAGMENT_SHADER, GlifWebGLRenderer.FRAG_SRC);
        this._program = gl.createProgram();
        gl.attachShader(this._program, vs);
        gl.attachShader(this._program, fs);
        gl.linkProgram(this._program);
        if (!gl.getProgramParameter(this._program, gl.LINK_STATUS)) {
            throw new Error('Shader link: ' + gl.getProgramInfoLog(this._program));
        }
        gl.deleteShader(vs);
        gl.deleteShader(fs);

        // Fullscreen quad
        this._quadBuf = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this._quadBuf);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
            -1, -1,  1, -1,  -1, 1,
            -1,  1,  1, -1,   1, 1
        ]), gl.STATIC_DRAW);

        // Upload font atlas
        this._atlasTex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this._atlasTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, atlasCanvas);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

        // Create data textures (will be resized on first render)
        this._charTex = this._createDataTexture();
        this._colorTex = this._createDataTexture();

        console.log('WebGL renderer active');
    }

    /**
     * Render a processed frame.
     * @param {{ rows, cols, chars, r, g, b }} result — output from GlifRenderer.processFrame()
     */
    render(result) {
        if (!result || !this._program) return;
        const gl = this.gl;
        const { rows, cols, chars, r, g, b } = result;

        // Physical (backing store) dimensions at device pixel ratio
        const physW = cols * this._physCharW;
        const physH = rows * this._physCharH;
        // Logical (CSS) dimensions
        const logicalW = cols * this._charW;
        const logicalH = rows * this._charH;

        if (this.canvas.width !== physW || this.canvas.height !== physH) {
            this.canvas.width = physW;
            this.canvas.height = physH;
            this.canvas.style.width = logicalW + 'px';
            this.canvas.style.height = logicalH + 'px';
        }
        gl.viewport(0, 0, physW, physH);

        this._gridCols = cols;
        this._gridRows = rows;

        // Upload character grid as single-channel (R) texture
        // Encode char index as: ch - 32 (ASCII 32..126 -> 0..94)
        const ncells = rows * cols;
        const charData = new Uint8Array(ncells);
        for (let i = 0; i < ncells; i++) {
            charData[i] = chars[i] - 32;
        }
        gl.bindTexture(gl.TEXTURE_2D, this._charTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, cols, rows, 0,
                      gl.LUMINANCE, gl.UNSIGNED_BYTE, charData);

        // Upload color grid as RGB texture
        const colorData = new Uint8Array(ncells * 3);
        for (let i = 0; i < ncells; i++) {
            colorData[i * 3]     = r[i];
            colorData[i * 3 + 1] = g[i];
            colorData[i * 3 + 2] = b[i];
        }
        gl.bindTexture(gl.TEXTURE_2D, this._colorTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, cols, rows, 0,
                      gl.RGB, gl.UNSIGNED_BYTE, colorData);

        // Draw
        gl.useProgram(this._program);

        const aPos = gl.getAttribLocation(this._program, 'a_pos');
        gl.bindBuffer(gl.ARRAY_BUFFER, this._quadBuf);
        gl.enableVertexAttribArray(aPos);
        gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

        // Bind textures
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this._charTex);
        gl.uniform1i(gl.getUniformLocation(this._program, 'u_charGrid'), 0);

        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, this._colorTex);
        gl.uniform1i(gl.getUniformLocation(this._program, 'u_colorGrid'), 1);

        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, this._atlasTex);
        gl.uniform1i(gl.getUniformLocation(this._program, 'u_fontAtlas'), 2);

        // Set uniforms (use physical pixel dimensions for shader)
        gl.uniform2f(gl.getUniformLocation(this._program, 'u_gridSize'),
                     cols, rows);
        gl.uniform2f(gl.getUniformLocation(this._program, 'u_atlasGrid'),
                     this._atlasGridX, this._atlasGridY);
        gl.uniform2f(gl.getUniformLocation(this._program, 'u_cellSize'),
                     this._physCharW, this._physCharH);
        gl.uniform2f(gl.getUniformLocation(this._program, 'u_resolution'),
                     physW, physH);

        gl.drawArrays(gl.TRIANGLES, 0, 6);
    }

    destroy() {
        const gl = this.gl;
        if (this._program) gl.deleteProgram(this._program);
        if (this._quadBuf) gl.deleteBuffer(this._quadBuf);
        if (this._charTex) gl.deleteTexture(this._charTex);
        if (this._colorTex) gl.deleteTexture(this._colorTex);
        if (this._atlasTex) gl.deleteTexture(this._atlasTex);
        this._program = null;
    }

    _compile(type, src) {
        const gl = this.gl;
        const s = gl.createShader(type);
        gl.shaderSource(s, src);
        gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
            throw new Error('Shader compile: ' + gl.getShaderInfoLog(s));
        }
        return s;
    }

    _createDataTexture() {
        const gl = this.gl;
        const tex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, tex);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return tex;
    }
}

GlifWebGLRenderer.VERT_SRC = `
attribute vec2 a_pos;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
`;

GlifWebGLRenderer.FRAG_SRC = `
precision mediump float;

uniform sampler2D u_charGrid;   // cols x rows, LUMINANCE = char index (0-94)
uniform sampler2D u_colorGrid;  // cols x rows, RGB = cell color
uniform sampler2D u_fontAtlas;  // atlas with 16x6 glyph grid
uniform vec2 u_gridSize;        // (cols, rows)
uniform vec2 u_atlasGrid;       // (16, 6)
uniform vec2 u_cellSize;        // pixel size of each cell
uniform vec2 u_resolution;      // canvas pixel dimensions

void main() {
    // Flip Y: WebGL origin is bottom-left, we want top-left
    vec2 fragCoord = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);

    // Which grid cell are we in?
    vec2 cellCoord = floor(fragCoord / u_cellSize);

    // Clamp to grid bounds
    cellCoord = clamp(cellCoord, vec2(0.0), u_gridSize - 1.0);

    // UV for grid lookup (center of texel)
    vec2 cellUV = (cellCoord + 0.5) / u_gridSize;

    // Look up character index and color
    float charIdx = texture2D(u_charGrid, cellUV).r * 255.0;
    vec3 color = texture2D(u_colorGrid, cellUV).rgb;

    // Position within the cell (0-1)
    vec2 inCell = fract(fragCoord / u_cellSize);

    // Map to font atlas UV
    float col = mod(charIdx, u_atlasGrid.x);
    float row = floor(charIdx / u_atlasGrid.x);
    vec2 atlasUV = (vec2(col, row) + inCell) / u_atlasGrid;

    float alpha = texture2D(u_fontAtlas, atlasUV).a;

    // Dark mode: black bg, colored glyph
    gl_FragColor = vec4(color * alpha, 1.0);
}
`;
