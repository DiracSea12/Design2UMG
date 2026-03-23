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
function toRestTransform(transform) {
    if (!transform || transform.length !== 2)
        return undefined;
    const row0 = transform[0];
    const row1 = transform[1];
    if (row0.length !== 3 || row1.length !== 3)
        return undefined;
    return [
        [row0[0], row0[1], row0[2]],
        [row1[0], row1[1], row1[2]],
    ];
}
function invertTransform(transform) {
    const [[a, b, tx], [c, d, ty]] = transform;
    const det = a * d - b * c;
    if (Math.abs(det) < 1e-6)
        return null;
    return [
        [d / det, -b / det, (b * ty - d * tx) / det],
        [-c / det, a / det, (c * tx - a * ty) / det],
    ];
}
function applyTransform(transform, x, y) {
    return {
        x: transform[0][0] * x + transform[0][1] * y + transform[0][2],
        y: transform[1][0] * x + transform[1][1] * y + transform[1][2],
    };
}
function gradientTransformToHandlePositions(transform) {
    const inverse = invertTransform(transform);
    if (!inverse)
        return [];
    return [
        applyTransform(inverse, 0, 0.5),
        applyTransform(inverse, 1, 0.5),
        applyTransform(inverse, 0, 1),
    ];
}
function cloneJsonSafe(value) {
    if (value === undefined)
        return undefined;
    try {
        return JSON.parse(JSON.stringify(value));
    }
    catch (_a) {
        return undefined;
    }
}
function documentationLinksToRest(links) {
    if (!links || links.length === 0)
        return undefined;
    return links.map(link => ({ uri: link.uri }));
}
function exposedInstancesToRest(instances) {
    if (!instances || instances.length === 0)
        return undefined;
    return instances.map(instance => ({
        id: instance.id,
        name: instance.name || instance.id,
        type: instance.type,
    }));
}
function setIfDefined(target, key, value) {
    if (value !== undefined && value !== null) {
        target[key] = value;
    }
}
function makeRenderFallbackKey(nodeId) {
    const sanitized = (nodeId || '')
        .replace(/[^a-zA-Z0-9]+/g, '_')
        .replace(/^_+|_+$/g, '');
    return `render_${sanitized || 'node'}`;
}
function indexRestNodes(node, out = new Map()) {
    if (!node)
        return out;
    out.set(node.id, node);
    for (const child of node.children || []) {
        indexRestNodes(child, out);
    }
    return out;
}
function hasVisibleChildNodes(node) {
    if (!('children' in node))
        return false;
    return (node.children || []).some(child => child.visible !== false);
}
function hasNodeReactions(node) {
    return 'reactions' in node && Array.isArray(node.reactions) && node.reactions.length > 0;
}
function isExcludedRenderFallbackNodeType(node) {
    return node.type === 'TEXT'
        || node.type === 'INSTANCE'
        || node.type === 'COMPONENT'
        || node.type === 'COMPONENT_SET'
        || node.type === 'SECTION';
}
function getRenderFallbackReasons(node) {
    if (!('fills' in node) || node.fills === figma.mixed) {
        return [];
    }
    const reasons = new Set();
    for (const fill of node.fills) {
        if (fill.type !== 'IMAGE' || fill.visible === false) {
            continue;
        }
        const imagePaint = fill;
        if (imagePaint.filters && Object.keys(imagePaint.filters).length > 0) {
            reasons.add('image_filters');
        }
        const blendMode = String(imagePaint.blendMode || 'NORMAL').toUpperCase();
        if (blendMode !== 'NORMAL' && blendMode !== 'PASS_THROUGH') {
            reasons.add(`image_blend_${blendMode.toLowerCase()}`);
        }
    }
    return Array.from(reasons.values());
}
function shouldExportNodeRenderFallback(node) {
    if (isExcludedRenderFallbackNodeType(node)) {
        return { use: false, reason: '' };
    }
    if (hasVisibleChildNodes(node)) {
        return { use: false, reason: '' };
    }
    if (hasNodeReactions(node)) {
        return { use: false, reason: '' };
    }
    const reasons = getRenderFallbackReasons(node);
    if (reasons.length === 0) {
        return { use: false, reason: '' };
    }
    return {
        use: true,
        reason: reasons.join('+'),
    };
}
function attachRenderFallbackMetadata(restNode, imageRef, reason) {
    if (!restNode)
        return;
    restNode.design2umgRenderFallbackImageRef = imageRef;
    restNode.design2umgRenderFallbackScope = 'full';
    restNode.design2umgRenderFallbackSource = 'figma_render_png';
    restNode.design2umgRenderFallbackReason = reason;
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
    setIfDefined(out, 'boundVariables', cloneJsonSafe(paint.boundVariables));
    if (paint.type === 'SOLID' && 'color' in paint) {
        out.color = colorToRest(paint.color, paint.opacity);
    }
    if (paint.type === 'GRADIENT_LINEAR' || paint.type === 'GRADIENT_RADIAL' ||
        paint.type === 'GRADIENT_ANGULAR' || paint.type === 'GRADIENT_DIAMOND') {
        const gp = paint;
        const gradientTransform = toRestTransform(gp.gradientTransform);
        if (gradientTransform) {
            out.gradientTransform = gradientTransform;
            out.gradientHandlePositions = gradientTransformToHandlePositions(gradientTransform);
        }
        else {
            out.gradientHandlePositions = [];
        }
        out.gradientStops = gp.gradientStops.map(s => ({
            color: colorToRest(s.color),
            position: s.position,
        }));
    }
    if (paint.type === 'IMAGE' && 'imageHash' in paint) {
        // Plugin API: imageHash → REST API: imageRef
        const imagePaint = paint;
        out.imageRef = imagePaint.imageHash || '';
        out.scaleMode = imagePaint.scaleMode || 'FILL';
        setIfDefined(out, 'rotation', imagePaint.rotation);
        setIfDefined(out, 'filters', cloneJsonSafe(imagePaint.filters));
        if (out.scaleMode === 'TILE') {
            setIfDefined(out, 'scalingFactor', imagePaint.scalingFactor);
        }
        const imageTransform = toRestTransform(imagePaint.imageTransform);
        if (out.scaleMode === 'CROP' && imageTransform) {
            out.imageTransform = imageTransform;
        }
    }
    return out;
}
// ── Effect Serialization ──
function serializeEffect(effect) {
    const out = {
        type: effect.type,
        visible: effect.visible !== false,
    };
    setIfDefined(out, 'blendMode', mapBlendMode(effect.blendMode || 'NORMAL'));
    setIfDefined(out, 'opacity', effect.opacity);
    if ('radius' in effect)
        out.radius = effect.radius;
    if ('offset' in effect)
        out.offset = effect.offset;
    if ('color' in effect)
        out.color = colorToRest(effect.color);
    if ('spread' in effect)
        out.spread = effect.spread;
    setIfDefined(out, 'showShadowBehindNode', effect.showShadowBehindNode);
    setIfDefined(out, 'boundVariables', cloneJsonSafe(effect.boundVariables));
    return out;
}
// ── Text Style Aggregation ──
function buildTextStyle(node) {
    var _a, _b;
    const style = {};
    style.fontFamily = node.fontName !== figma.mixed ? node.fontName.family : 'Inter';
    style.fontPostScriptName = '';
    style.fontWeight = node.fontWeight !== figma.mixed ? node.fontWeight : 400;
    style.fontSize = node.fontSize !== figma.mixed ? node.fontSize : 14;
    style.textAlignHorizontal = node.textAlignHorizontal || 'LEFT';
    style.textAlignVertical = node.textAlignVertical || 'TOP';
    style.textCase = node.textCase !== figma.mixed ? node.textCase : 'ORIGINAL';
    style.textDecoration = node.textDecoration !== figma.mixed ? node.textDecoration : 'NONE';
    style.letterSpacing = node.letterSpacing !== figma.mixed
        ? node.letterSpacing.value || 0 : 0;
    style.paragraphSpacing = (_a = node.paragraphSpacing) !== null && _a !== void 0 ? _a : 0;
    style.paragraphIndent = (_b = node.paragraphIndent) !== null && _b !== void 0 ? _b : 0;
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
    if (node.isMask && 'maskType' in node) {
        setIfDefined(out, 'maskType', node.maskType);
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
    setIfDefined(out, 'boundVariables', cloneJsonSafe(node.boundVariables));
    setIfDefined(out, 'componentPropertyReferences', cloneJsonSafe(node.componentPropertyReferences));
    setIfDefined(out, 'explicitVariableModes', cloneJsonSafe(node.explicitVariableModes));
    setIfDefined(out, 'resolvedVariableModes', cloneJsonSafe(node.resolvedVariableModes));
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
        setIfDefined(out, 'counterAxisSpacing', n.counterAxisSpacing);
        setIfDefined(out, 'counterAxisAlignContent', n.counterAxisAlignContent);
        setIfDefined(out, 'layoutWrap', n.layoutWrap);
        setIfDefined(out, 'strokesIncludedInLayout', n.strokesIncludedInLayout);
        setIfDefined(out, 'itemReverseZIndex', n.itemReverseZIndex);
        setIfDefined(out, 'gridRowCount', n.gridRowCount);
        setIfDefined(out, 'gridColumnCount', n.gridColumnCount);
        setIfDefined(out, 'gridRowGap', n.gridRowGap);
        setIfDefined(out, 'gridColumnGap', n.gridColumnGap);
        setIfDefined(out, 'gridRowsSizing', n.gridRowsSizing);
        setIfDefined(out, 'gridColumnsSizing', n.gridColumnsSizing);
        setIfDefined(out, 'gridChildHorizontalAlign', n.gridChildHorizontalAlign);
        setIfDefined(out, 'gridChildVerticalAlign', n.gridChildVerticalAlign);
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
    if ('layoutPositioning' in node)
        out.layoutPositioning = node.layoutPositioning;
    if ('minWidth' in node)
        setIfDefined(out, 'minWidth', node.minWidth);
    if ('maxWidth' in node)
        setIfDefined(out, 'maxWidth', node.maxWidth);
    if ('minHeight' in node)
        setIfDefined(out, 'minHeight', node.minHeight);
    if ('maxHeight' in node)
        setIfDefined(out, 'maxHeight', node.maxHeight);
    if ('gridRowSpan' in node)
        setIfDefined(out, 'gridRowSpan', node.gridRowSpan);
    if ('gridColumnSpan' in node)
        setIfDefined(out, 'gridColumnSpan', node.gridColumnSpan);
    if ('gridColumnAnchorIndex' in node)
        setIfDefined(out, 'gridColumnAnchorIndex', node.gridColumnAnchorIndex);
    if ('gridRowAnchorIndex' in node)
        setIfDefined(out, 'gridRowAnchorIndex', node.gridRowAnchorIndex);
    // Component reference
    if (node.type === 'INSTANCE') {
        const instanceNode = node;
        if (instanceNode.mainComponent) {
            out.componentId = instanceNode.mainComponent.id;
            setIfDefined(out, 'mainComponentKey', instanceNode.mainComponent.key);
            const componentSet = instanceNode.mainComponent.parent;
            if (componentSet && componentSet.type === 'COMPONENT_SET') {
                out.componentSetId = componentSet.id;
            }
        }
        setIfDefined(out, 'componentProperties', cloneJsonSafe(instanceNode.componentProperties));
        setIfDefined(out, 'variantProperties', cloneJsonSafe(instanceNode.variantProperties));
        setIfDefined(out, 'overrides', cloneJsonSafe(instanceNode.overrides));
        setIfDefined(out, 'exposedInstances', exposedInstancesToRest(instanceNode.exposedInstances));
        setIfDefined(out, 'isExposedInstance', instanceNode.isExposedInstance);
    }
    // Text
    if (node.type === 'TEXT') {
        const tn = node;
        out.characters = tn.characters;
        out.style = buildTextStyle(tn);
        out.textAutoResize = tn.textAutoResize || 'NONE';
        setIfDefined(out, 'textTruncation', tn.textTruncation);
        setIfDefined(out, 'maxLines', tn.maxLines);
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
function findNodeById(node, targetId) {
    if (!node || typeof node !== 'object')
        return null;
    if (node.id === targetId)
        return node;
    const children = Array.isArray(node.children) ? node.children : [];
    for (const child of children) {
        const match = findNodeById(child, targetId);
        if (match)
            return match;
    }
    return null;
}
function unwrapRestExportResponse(response, targetId) {
    if (!response || typeof response !== 'object')
        return null;
    const directDocument = response.document;
    if (directDocument && typeof directDocument === 'object') {
        const directMatch = findNodeById(directDocument, targetId);
        return directMatch || directDocument;
    }
    const nodes = response.nodes;
    if (nodes && typeof nodes === 'object') {
        const candidateKeys = [targetId, targetId.replace(/:/g, '-')];
        for (const key of candidateKeys) {
            const entry = nodes[key];
            if (entry && typeof entry === 'object' && entry.document) {
                return entry.document;
            }
        }
        for (const entry of Object.values(nodes)) {
            if (entry && typeof entry === 'object' && entry.document) {
                const match = findNodeById(entry.document, targetId);
                if (match)
                    return match;
            }
        }
    }
    return null;
}
async function exportOfficialRestNode(node) {
    if (!('exportAsync' in node))
        return null;
    try {
        const response = await node.exportAsync({ format: 'JSON_REST_V1' });
        return unwrapRestExportResponse(response, node.id);
    }
    catch (error) {
        console.warn(`JSON_REST_V1 export failed for ${node.name}:`, error);
        return null;
    }
}
function isVariantComponent(component) {
    return !!(component.parent && component.parent.type === 'COMPONENT_SET');
}
function getExportableComponentPropertyDefinitions(component) {
    if (isVariantComponent(component)) {
        return undefined;
    }
    return cloneJsonSafe(component.componentPropertyDefinitions);
}
function collectReferencedComponents(node, components, componentSets) {
    const registerComponent = (component) => {
        components[component.id] = {
            key: component.key,
            name: component.name,
            description: component.description || '',
            remote: !!component.remote,
            componentSetId: component.parent && component.parent.type === 'COMPONENT_SET' ? component.parent.id : undefined,
            componentPropertyDefinitions: getExportableComponentPropertyDefinitions(component),
            documentationLinks: documentationLinksToRest(component.documentationLinks),
        };
        if (component.parent && component.parent.type === 'COMPONENT_SET') {
            const componentSet = component.parent;
            componentSets[componentSet.id] = {
                key: componentSet.key,
                name: componentSet.name,
                description: componentSet.description || '',
                remote: !!componentSet.remote,
                componentPropertyDefinitions: cloneJsonSafe(componentSet.componentPropertyDefinitions),
                variantGroupProperties: cloneJsonSafe(componentSet.variantGroupProperties),
                documentationLinks: documentationLinksToRest(componentSet.documentationLinks),
            };
        }
    };
    if (node.type === 'COMPONENT') {
        registerComponent(node);
    }
    else if (node.type === 'COMPONENT_SET') {
        componentSets[node.id] = {
            key: node.key,
            name: node.name,
            description: node.description || '',
            remote: !!node.remote,
            componentPropertyDefinitions: cloneJsonSafe(node.componentPropertyDefinitions),
            variantGroupProperties: cloneJsonSafe(node.variantGroupProperties),
            documentationLinks: documentationLinksToRest(node.documentationLinks),
        };
    }
    else if (node.type === 'INSTANCE' && node.mainComponent) {
        registerComponent(node.mainComponent);
    }
    if ('children' in node) {
        for (const child of node.children) {
            collectReferencedComponents(child, components, componentSets);
        }
    }
}
async function collectImages(node, images, scale, onProgress, restNodesById) {
    const renderFallback = shouldExportNodeRenderFallback(node);
    if (renderFallback.use && 'exportAsync' in node) {
        const imageRef = makeRenderFallbackKey(node.id);
        if (!images.has(imageRef)) {
            try {
                const bytes = await node.exportAsync({
                    format: 'PNG',
                    constraint: { type: 'SCALE', value: scale },
                });
                images.set(imageRef, bytes);
                onProgress(`Render: ${node.name}`);
            }
            catch (e) {
                console.warn(`Failed to export render fallback ${node.name}:`, e);
            }
        }
        if (images.has(imageRef)) {
            attachRenderFallbackMetadata(restNodesById === null || restNodesById === void 0 ? void 0 : restNodesById.get(node.id), imageRef, renderFallback.reason);
        }
    }
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
    if ('strokes' in node) {
        for (const stroke of node.strokes || []) {
            if (stroke.type === 'IMAGE' && 'imageHash' in stroke) {
                const hash = stroke.imageHash;
                if (hash && !images.has(hash)) {
                    try {
                        const img = figma.getImageByHash(hash);
                        if (img) {
                            const bytes = await img.getBytesAsync();
                            images.set(hash, bytes);
                            onProgress(`Stroke Image: ${hash.substring(0, 8)}...`);
                        }
                    }
                    catch (e) {
                        console.warn(`Failed to get stroke image ${hash}:`, e);
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
            await collectImages(child, images, scale, onProgress, restNodesById);
        }
    }
}
const testApi = {
    attachRenderFallbackMetadata,
    indexRestNodes,
    makeRenderFallbackKey,
    serializePaint,
    serializeEffect,
    buildTextStyle,
    serializeNode,
    unwrapRestExportResponse,
    collectReferencedComponents,
    collectImages,
    shouldExportNodeRenderFallback,
};
if (typeof globalThis !== 'undefined') {
    globalThis.__design2umgExporterTestApi = testApi;
}
// ── Main Export Flow ──
if (typeof figma !== 'undefined') {
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
            figma.ui.postMessage({ type: 'progress', percent: 10, text: 'Serializing nodes...' });
            const serialized = await exportOfficialRestNode(root) || serializeNode(root);
            if (!serialized) {
                figma.ui.postMessage({ type: 'error', text: 'Failed to serialize selected node.' });
                return;
            }
            const components = {};
            const componentSets = {};
            collectReferencedComponents(root, components, componentSets);
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
            if (Object.keys(components).length > 0) {
                document.components = components;
            }
            if (Object.keys(componentSets).length > 0) {
                document.componentSets = componentSets;
            }
            figma.ui.postMessage({ type: 'progress', percent: 30, text: 'Collecting images...' });
            const images = new Map();
            const restNodesById = indexRestNodes(serialized);
            await collectImages(root, images, scale, (text) => {
                figma.ui.postMessage({ type: 'progress', percent: 50, text });
            }, restNodesById);
            figma.ui.postMessage({ type: 'progress', percent: 80, text: 'Preparing download...' });
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
}
