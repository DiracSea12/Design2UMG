"use strict";
// Design2UMG Figma Exporter - Plugin Code
// Traverses selected frame, serializes to REST API compatible JSON, exports images
// ── Helpers ──
function colorToRest(c, opacity = 1) {
    return { r: c.r, g: c.g, b: c.b, a: 'a' in c ? c.a * opacity : opacity };
}
function rectToRest(bb) {
    return { x: bb.x, y: bb.y, width: bb.width, height: bb.height };
}
function mapBlendMode(bm) {
    return bm.replace(/_/g, '-');
}
// ── Fill Serialization ──
function serializePaint(paint) {
    var _a;
    const out = {
        type: paint.type,
        visible: paint.visible !== false,
        opacity: (_a = paint.opacity) !== null && _a !== void 0 ? _a : 1,
        blendMode: mapBlendMode(paint.blendMode || 'NORMAL'),
    };
    if (paint.type === 'SOLID' && 'color' in paint) {
        out.color = colorToRest(paint.color, paint.opacity);
    }
    if (paint.type === 'GRADIENT_LINEAR' || paint.type === 'GRADIENT_RADIAL' ||
        paint.type === 'GRADIENT_ANGULAR' || paint.type === 'GRADIENT_DIAMOND') {
        const gp = paint;
        out.gradientHandlePositions = gp.gradientTransform
            ? [{ x: gp.gradientTransform[0][2], y: gp.gradientTransform[1][2] }]
            : [];
        out.gradientStops = gp.gradientStops.map(s => ({
            color: colorToRest(s.color),
            position: s.position,
        }));
    }
    if (paint.type === 'IMAGE' && 'imageHash' in paint) {
        // Plugin API: imageHash → REST API: imageRef
        out.imageRef = paint.imageHash || '';
        out.scaleMode = paint.scaleMode || 'FILL';
    }
    return out;
}
// ── Effect Serialization ──
function serializeEffect(effect) {
    const out = {
        type: effect.type,
        visible: effect.visible !== false,
    };
    if ('radius' in effect)
        out.radius = effect.radius;
    if ('offset' in effect)
        out.offset = effect.offset;
    if ('color' in effect)
        out.color = colorToRest(effect.color);
    if ('spread' in effect)
        out.spread = effect.spread;
    return out;
}
// ── Text Style Aggregation ──
function buildTextStyle(node) {
    const style = {};
    style.fontFamily = node.fontName !== figma.mixed ? node.fontName.family : 'Inter';
    style.fontPostScriptName = '';
    style.fontWeight = node.fontWeight !== figma.mixed ? node.fontWeight : 400;
    style.fontSize = node.fontSize !== figma.mixed ? node.fontSize : 14;
    style.textAlignHorizontal = node.textAlignHorizontal || 'LEFT';
    style.textAlignVertical = node.textAlignVertical || 'TOP';
    style.letterSpacing = node.letterSpacing !== figma.mixed
        ? node.letterSpacing.value || 0 : 0;
    if (node.lineHeight !== figma.mixed) {
        const lh = node.lineHeight;
        style.lineHeightUnit = lh.unit === 'PIXELS' ? 'PIXELS' : lh.unit === 'PERCENT' ? 'FONT_SIZE_%' : 'AUTO';
        style.lineHeightPx = lh.unit === 'PIXELS' ? lh.value : 0;
    }
    else {
        style.lineHeightUnit = 'AUTO';
        style.lineHeightPx = 0;
    }
    return style;
}
// ── Node Serialization ──
function serializeNode(node) {
    var _a, _b, _c, _d;
    if (!node.visible && node.type !== 'COMPONENT' && node.type !== 'INSTANCE')
        return null;
    const out = {
        id: node.id,
        name: node.name,
        type: node.type,
        visible: node.visible,
    };
    // absoluteBoundingBox — Plugin API provides this directly
    if ('absoluteBoundingBox' in node && node.absoluteBoundingBox) {
        out.absoluteBoundingBox = rectToRest(node.absoluteBoundingBox);
    }
    // absoluteRenderBounds — includes stroke/effect expansion
    if ('absoluteRenderBounds' in node) {
        const rb = node.absoluteRenderBounds;
        out.absoluteRenderBounds = rb ? rectToRest(rb) : null;
    }
    // relativeTransform — used for flip detection
    if ('relativeTransform' in node) {
        const t = node.relativeTransform;
        out.relativeTransform = [[t[0][0], t[0][1], t[0][2]], [t[1][0], t[1][1], t[1][2]]];
    }
    // rotation
    if ('rotation' in node) {
        out.rotation = node.rotation;
    }
    // isMask
    if ('isMask' in node) {
        out.isMask = node.isMask;
    }
    // Size
    if ('width' in node) {
        out.size = { x: node.width, y: node.height };
    }
    // Fills & Strokes
    if ('fills' in node && node.fills !== figma.mixed) {
        out.fills = node.fills.map(serializePaint);
    }
    if ('strokes' in node) {
        out.strokes = node.strokes.map(serializePaint);
    }
    // Geometry paths (fillGeometry / strokeGeometry)
    if ('fillGeometry' in node) {
        const fg = node.fillGeometry;
        if (fg && fg.length > 0) {
            out.fillGeometry = fg.map((p) => ({ path: p.data, windingRule: p.windingRule }));
        }
    }
    if ('strokeGeometry' in node) {
        const sg = node.strokeGeometry;
        if (sg && sg.length > 0) {
            out.strokeGeometry = sg.map((p) => ({ path: p.data, windingRule: p.windingRule }));
        }
    }
    // Stroke weight
    if ('strokeWeight' in node) {
        out.strokeWeight = node.strokeWeight !== figma.mixed ? node.strokeWeight : 1;
        out.strokeAlign = node.strokeAlign || 'INSIDE';
    }
    // Individual stroke weights → aggregate to object
    if ('strokeTopWeight' in node) {
        const n = node;
        out.individualStrokeWeights = {
            top: (_a = n.strokeTopWeight) !== null && _a !== void 0 ? _a : 0,
            right: (_b = n.strokeRightWeight) !== null && _b !== void 0 ? _b : 0,
            bottom: (_c = n.strokeBottomWeight) !== null && _c !== void 0 ? _c : 0,
            left: (_d = n.strokeLeftWeight) !== null && _d !== void 0 ? _d : 0,
        };
    }
    // Effects
    if ('effects' in node) {
        out.effects = node.effects.map(serializeEffect);
    }
    // Corner radius
    if ('cornerRadius' in node) {
        const cr = node.cornerRadius;
        if (cr !== figma.mixed)
            out.cornerRadius = cr;
    }
    if ('topLeftRadius' in node) {
        const n = node;
        out.rectangleCornerRadii = [n.topLeftRadius, n.topRightRadius, n.bottomRightRadius, n.bottomLeftRadius];
    }
    // Opacity & blend
    if ('opacity' in node)
        out.opacity = node.opacity;
    if ('blendMode' in node)
        out.blendMode = mapBlendMode(node.blendMode);
    // Clips
    if ('clipsContent' in node)
        out.clipsContent = node.clipsContent;
    // Constraints
    if ('constraints' in node) {
        const c = node.constraints;
        out.constraints = { vertical: c.vertical, horizontal: c.horizontal };
    }
    // Auto Layout
    if ('layoutMode' in node && node.layoutMode !== 'NONE') {
        const n = node;
        out.layoutMode = n.layoutMode;
        out.primaryAxisSizingMode = n.primaryAxisSizingMode;
        out.counterAxisSizingMode = n.counterAxisSizingMode;
        out.primaryAxisAlignItems = n.primaryAxisAlignItems;
        out.counterAxisAlignItems = n.counterAxisAlignItems;
        out.paddingLeft = n.paddingLeft;
        out.paddingRight = n.paddingRight;
        out.paddingTop = n.paddingTop;
        out.paddingBottom = n.paddingBottom;
        out.itemSpacing = n.itemSpacing;
    }
    // Layout child properties
    if ('layoutAlign' in node)
        out.layoutAlign = node.layoutAlign;
    if ('layoutGrow' in node)
        out.layoutGrow = node.layoutGrow;
    if ('layoutSizingHorizontal' in node)
        out.layoutSizingHorizontal = node.layoutSizingHorizontal;
    if ('layoutSizingVertical' in node)
        out.layoutSizingVertical = node.layoutSizingVertical;
    // Component reference
    if (node.type === 'INSTANCE' && node.mainComponent) {
        out.componentId = node.mainComponent.id;
    }
    // Text
    if (node.type === 'TEXT') {
        const tn = node;
        out.characters = tn.characters;
        out.style = buildTextStyle(tn);
        out.textAutoResize = tn.textAutoResize || 'NONE';
    }
    // Children
    if ('children' in node) {
        const children = [];
        for (const child of node.children) {
            const serialized = serializeNode(child);
            if (serialized)
                children.push(serialized);
        }
        out.children = children;
    }
    return out;
}
async function collectImages(node, images, scale, onProgress) {
    // Image fills
    if ('fills' in node && node.fills !== figma.mixed) {
        for (const fill of node.fills) {
            if (fill.type === 'IMAGE' && 'imageHash' in fill) {
                const hash = fill.imageHash;
                if (hash && !images.has(hash)) {
                    try {
                        const img = figma.getImageByHash(hash);
                        if (img) {
                            const bytes = await img.getBytesAsync();
                            images.set(hash, bytes);
                            onProgress(`Image: ${hash.substring(0, 8)}...`);
                        }
                    }
                    catch (e) {
                        console.warn(`Failed to get image ${hash}:`, e);
                    }
                }
            }
        }
    }
    // Vector nodes → export as PNG
    const vectorTypes = ['VECTOR', 'BOOLEAN_OPERATION', 'STAR', 'LINE', 'ELLIPSE', 'POLYGON'];
    if (vectorTypes.includes(node.type) && 'exportAsync' in node) {
        const vecKey = `vec_${node.id.replace(/[:/]/g, '_')}`;
        if (!images.has(vecKey)) {
            try {
                const bytes = await node.exportAsync({
                    format: 'PNG',
                    constraint: { type: 'SCALE', value: scale },
                });
                images.set(vecKey, bytes);
                onProgress(`Vector: ${node.name}`);
            }
            catch (e) {
                console.warn(`Failed to export vector ${node.name}:`, e);
            }
        }
    }
    // Recurse children
    if ('children' in node) {
        for (const child of node.children) {
            await collectImages(child, images, scale, onProgress);
        }
    }
}
// ── Main Export Flow ──
figma.showUI(__html__, { width: 360, height: 200 });
figma.ui.onmessage = async (msg) => {
    if (msg.type !== 'export')
        return;
    const selection = figma.currentPage.selection;
    if (selection.length === 0) {
        figma.ui.postMessage({ type: 'error', text: 'No frame selected. Please select a frame first.' });
        return;
    }
    const root = selection[0];
    if (root.type !== 'FRAME' && root.type !== 'COMPONENT' && root.type !== 'INSTANCE') {
        figma.ui.postMessage({ type: 'error', text: 'Please select a Frame, Component, or Instance.' });
        return;
    }
    const scale = msg.scale || 2;
    try {
        // Step 1: Serialize node tree
        figma.ui.postMessage({ type: 'progress', percent: 10, text: 'Serializing nodes...' });
        const serialized = serializeNode(root);
        if (!serialized) {
            figma.ui.postMessage({ type: 'error', text: 'Failed to serialize selected node.' });
            return;
        }
        // Wrap in REST API document structure
        const document = {
            document: {
                id: '0:0',
                name: figma.root.name,
                type: 'DOCUMENT',
                children: [{
                        id: figma.currentPage.id,
                        name: figma.currentPage.name,
                        type: 'CANVAS',
                        children: [serialized],
                    }],
            },
            name: figma.root.name,
            version: '0',
            schemaVersion: 0,
        };
        // Step 2: Collect images
        figma.ui.postMessage({ type: 'progress', percent: 30, text: 'Collecting images...' });
        const images = new Map();
        await collectImages(root, images, scale, (text) => {
            figma.ui.postMessage({ type: 'progress', percent: 50, text });
        });
        // Step 3: Send to UI for saving
        figma.ui.postMessage({ type: 'progress', percent: 80, text: 'Preparing download...' });
        // Convert images to transferable format
        const imageEntries = [];
        for (const [key, bytes] of images) {
            imageEntries.push({ name: key, data: Array.from(bytes) });
        }
        figma.ui.postMessage({
            type: 'done',
            text: `Exported ${images.size} image(s). Saving...`,
            json: JSON.stringify(document, null, 2),
            images: imageEntries,
            exportPath: msg.exportPath,
        });
    }
    catch (e) {
        figma.ui.postMessage({ type: 'error', text: `Export failed: ${e.message || e}` });
    }
};
