export interface Cell {
    ch: string;
    r: number;
    g: number;
    b: number;
}

export interface RenderOptions {
    cellWidth?: number;
    cellHeight?: number;
    dirCrunch?: number;
    globalCrunch?: number;
}

export interface RenderResult {
    rows: number;
    cols: number;
    cells: Cell[];
    toPlainText(): string;
    toAnsi(): string;
}

/**
 * Render an image to ASCII art.
 */
export function render(imagePath: string, fontPath: string, options?: RenderOptions): RenderResult;

/** Low-level: wraps C Image struct. */
export class Image {
    constructor(path: string);
    readonly width: number;
    readonly height: number;
    readonly channels: number;
    free(): void;
}

/** Low-level: wraps C LightnessMap struct. */
export class LightnessMap {
    constructor(image: Image);
    readonly width: number;
    readonly height: number;
    normalize(): void;
    free(): void;
}

/** Low-level: wraps C SamplingConfig struct. */
export class SamplingConfig {
    constructor();
}

/** Low-level: wraps C PrecomputedMasks struct. */
export class PrecomputedMasks {
    constructor(sc: SamplingConfig, cellW: number, cellH: number, stride: number);
    free(): void;
}

/** Low-level: wraps C CharDatabase struct. */
export class CharDatabase {
    constructor(fontPath: string, cellW: number, cellH: number, sc: SamplingConfig);
    free(): void;
}

/** Low-level: wraps C Grid struct. */
export class Grid {
    constructor(image: Image, cellW: number, cellH: number);
    readonly rows: number;
    readonly cols: number;
    computeVectorsFast(lm: LightnessMap, pm: PrecomputedMasks): void;
    computeColors(image: Image): void;
    applyDirectionalContrast(sc: SamplingConfig, crunch: number): void;
    applyGlobalContrast(crunch: number): void;
    match(db: CharDatabase): void;
    getCells(): Cell[];
    free(): void;
}
