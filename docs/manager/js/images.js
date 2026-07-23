export async function imageSourceFromBlob(blob) {
  if (!blob) throw new Error("Choose an image first.");
  if (typeof createImageBitmap === "function") {
    try {
      return await createImageBitmap(blob);
    } catch {
      // Some browsers cannot decode older BMP variants with createImageBitmap.
    }
  }
  return loadImageElement(blob);
}

function loadImageElement(blob) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(blob);
    const image = new Image();
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("That image could not be opened by this browser."));
    };
    image.src = url;
  });
}

function sourceDimensions(source) {
  return {
    width: source.width || source.naturalWidth,
    height: source.height || source.naturalHeight,
  };
}

function fillCheckerboard(context, width, height) {
  const square = 4;
  const colors = ["#a8a8a8", "#d8d8d8"];
  for (let y = 0; y < height; y += square) {
    for (let x = 0; x < width; x += square) {
      context.fillStyle = colors[((x / square) + (y / square)) % 2];
      context.fillRect(x, y, square, square);
    }
  }
}

function fitFillColor(value) {
  return /^#[0-9a-f]{6}$/i.test(String(value || "")) ? value : "#000000";
}

function drawMirroredRegion(context, source, sourceRect, targetRect, flipX = false, flipY = false) {
  context.save();
  context.translate(
    targetRect.x + (flipX ? targetRect.width : 0),
    targetRect.y + (flipY ? targetRect.height : 0),
  );
  context.scale(flipX ? -1 : 1, flipY ? -1 : 1);
  context.drawImage(
    source,
    sourceRect.x,
    sourceRect.y,
    sourceRect.width,
    sourceRect.height,
    0,
    0,
    targetRect.width,
    targetRect.height,
  );
  context.restore();
}

function drawExtendedArtworkBackground(context, source, sourceWidth, sourceHeight, targetWidth, targetHeight, rect) {
  const leftGap = Math.max(0, rect.x);
  const topGap = Math.max(0, rect.y);
  const rightGap = Math.max(0, targetWidth - rect.x - rect.width);
  const bottomGap = Math.max(0, targetHeight - rect.y - rect.height);
  const edgeWidth = Math.max(1, Math.min(8, Math.round(sourceWidth * 0.02)));
  const edgeHeight = Math.max(1, Math.min(8, Math.round(sourceHeight * 0.02)));
  const underlap = Math.max(1, Math.min(5, Math.floor(rect.width / 2), Math.floor(rect.height / 2)));

  context.save();
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";

  if (leftGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: 0, y: 0, width: edgeWidth, height: sourceHeight },
      { x: 0, y: rect.y, width: leftGap, height: rect.height },
      true,
    );
  }
  if (rightGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: sourceWidth - edgeWidth, y: 0, width: edgeWidth, height: sourceHeight },
      { x: rect.x + rect.width, y: rect.y, width: rightGap, height: rect.height },
      true,
    );
  }
  if (topGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: 0, y: 0, width: sourceWidth, height: edgeHeight },
      { x: rect.x, y: 0, width: rect.width, height: topGap },
      false,
      true,
    );
  }
  if (bottomGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: 0, y: sourceHeight - edgeHeight, width: sourceWidth, height: edgeHeight },
      { x: rect.x, y: rect.y + rect.height, width: rect.width, height: bottomGap },
      false,
      true,
    );
  }

  if (leftGap > 0 && topGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: 0, y: 0, width: edgeWidth, height: edgeHeight },
      { x: 0, y: 0, width: leftGap, height: topGap },
      true,
      true,
    );
  }
  if (rightGap > 0 && topGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: sourceWidth - edgeWidth, y: 0, width: edgeWidth, height: edgeHeight },
      { x: rect.x + rect.width, y: 0, width: rightGap, height: topGap },
      true,
      true,
    );
  }
  if (leftGap > 0 && bottomGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: 0, y: sourceHeight - edgeHeight, width: edgeWidth, height: edgeHeight },
      { x: 0, y: rect.y + rect.height, width: leftGap, height: bottomGap },
      true,
      true,
    );
  }
  if (rightGap > 0 && bottomGap > 0) {
    drawMirroredRegion(
      context,
      source,
      { x: sourceWidth - edgeWidth, y: sourceHeight - edgeHeight, width: edgeWidth, height: edgeHeight },
      { x: rect.x + rect.width, y: rect.y + rect.height, width: rightGap, height: bottomGap },
      true,
      true,
    );
  }

  // Continue the sampled edge beneath the optional feather so the blend
  // reveals matching artwork instead of the canvas fallback colour.
  if (leftGap > 0) {
    context.drawImage(
      source,
      0,
      0,
      edgeWidth,
      sourceHeight,
      rect.x,
      rect.y,
      underlap,
      rect.height,
    );
  }
  if (rightGap > 0) {
    context.drawImage(
      source,
      sourceWidth - edgeWidth,
      0,
      edgeWidth,
      sourceHeight,
      rect.x + rect.width - underlap,
      rect.y,
      underlap,
      rect.height,
    );
  }
  if (topGap > 0) {
    context.drawImage(
      source,
      0,
      0,
      sourceWidth,
      edgeHeight,
      rect.x,
      rect.y,
      rect.width,
      underlap,
    );
  }
  if (bottomGap > 0) {
    context.drawImage(
      source,
      0,
      sourceHeight - edgeHeight,
      sourceWidth,
      edgeHeight,
      rect.x,
      rect.y + rect.height - underlap,
      rect.width,
      underlap,
    );
  }
  context.restore();
}

function drawFitBackground(context, source, sourceWidth, sourceHeight, width, height, mode, color, rect) {
  if (mode === "checkerboard") {
    fillCheckerboard(context, width, height);
    return;
  }

  context.fillStyle = mode === "solid" ? fitFillColor(color) : "#000000";
  context.fillRect(0, 0, width, height);
  if (mode === "blend") {
    drawExtendedArtworkBackground(context, source, sourceWidth, sourceHeight, width, height, rect);
    return;
  }
  if (mode !== "blur") return;

  const padding = 6;
  context.save();
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.filter = "blur(4px)";
  context.drawImage(source, -padding, -padding, width + (padding * 2), height + (padding * 2));
  context.restore();
}

function createImageLayer(width, height) {
  if (typeof OffscreenCanvas === "function") return new OffscreenCanvas(width, height);
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  return canvas;
}

function featherFittedImage(layerContext, rect, targetWidth, targetHeight) {
  const feather = Math.max(1, Math.min(5, Math.floor(rect.width / 2), Math.floor(rect.height / 2)));
  layerContext.globalCompositeOperation = "destination-out";

  if (rect.x > 0.01) {
    const gradient = layerContext.createLinearGradient(rect.x, 0, rect.x + feather, 0);
    gradient.addColorStop(0, "rgba(0, 0, 0, 1)");
    gradient.addColorStop(1, "rgba(0, 0, 0, 0)");
    layerContext.fillStyle = gradient;
    layerContext.fillRect(rect.x, rect.y, feather, rect.height);
  }
  if (rect.x + rect.width < targetWidth - 0.01) {
    const edge = rect.x + rect.width;
    const gradient = layerContext.createLinearGradient(edge - feather, 0, edge, 0);
    gradient.addColorStop(0, "rgba(0, 0, 0, 0)");
    gradient.addColorStop(1, "rgba(0, 0, 0, 1)");
    layerContext.fillStyle = gradient;
    layerContext.fillRect(edge - feather, rect.y, feather, rect.height);
  }
  if (rect.y > 0.01) {
    const gradient = layerContext.createLinearGradient(0, rect.y, 0, rect.y + feather);
    gradient.addColorStop(0, "rgba(0, 0, 0, 1)");
    gradient.addColorStop(1, "rgba(0, 0, 0, 0)");
    layerContext.fillStyle = gradient;
    layerContext.fillRect(rect.x, rect.y, rect.width, feather);
  }
  if (rect.y + rect.height < targetHeight - 0.01) {
    const edge = rect.y + rect.height;
    const gradient = layerContext.createLinearGradient(0, edge - feather, 0, edge);
    gradient.addColorStop(0, "rgba(0, 0, 0, 0)");
    gradient.addColorStop(1, "rgba(0, 0, 0, 1)");
    layerContext.fillStyle = gradient;
    layerContext.fillRect(rect.x, edge - feather, rect.width, feather);
  }
}

function drawFittedImage(context, source, rect, targetWidth, targetHeight, blendBorder) {
  if (!blendBorder) {
    context.drawImage(source, rect.x, rect.y, rect.width, rect.height);
    return;
  }

  const layer = createImageLayer(targetWidth, targetHeight);
  const layerContext = layer.getContext("2d", { alpha: true });
  layerContext.imageSmoothingEnabled = true;
  layerContext.imageSmoothingQuality = "high";
  layerContext.drawImage(source, rect.x, rect.y, rect.width, rect.height);
  featherFittedImage(layerContext, rect, targetWidth, targetHeight);
  context.drawImage(layer, 0, 0);
}

export function drawCroppedImage(canvas, source, transform = {}) {
  const context = canvas.getContext("2d", { alpha: false, willReadFrequently: true });
  const { width: sourceWidth, height: sourceHeight } = sourceDimensions(source);
  const targetWidth = canvas.width;
  const targetHeight = canvas.height;
  const zoom = Math.max(1, Number(transform.zoom || 1));
  const panX = Math.max(-1, Math.min(1, Number(transform.x || 0)));
  const panY = Math.max(-1, Math.min(1, Number(transform.y || 0)));
  const fit = Boolean(transform.fit);
  const fillMode = ["solid", "checkerboard", "blur", "blend"].includes(transform.fillMode)
    ? transform.fillMode
    : "solid";
  const blendBorder = Boolean(transform.blendBorder);

  context.save();
  context.fillStyle = "#000";
  context.fillRect(0, 0, targetWidth, targetHeight);
  if (!sourceWidth || !sourceHeight) {
    context.restore();
    return;
  }

  const baseScale = fit
    ? Math.min(targetWidth / sourceWidth, targetHeight / sourceHeight)
    : Math.max(targetWidth / sourceWidth, targetHeight / sourceHeight);
  const scale = baseScale * (fit ? 1 : zoom);
  const drawWidth = sourceWidth * scale;
  const drawHeight = sourceHeight * scale;
  const availableX = drawWidth - targetWidth;
  const availableY = drawHeight - targetHeight;
  const drawX = fit
    ? (targetWidth - drawWidth) / 2
    : -Math.max(0, availableX) * ((panX + 1) / 2);
  const drawY = fit
    ? (targetHeight - drawHeight) / 2
    : -Math.max(0, availableY) * ((panY + 1) / 2);
  const rect = { x: drawX, y: drawY, width: drawWidth, height: drawHeight };

  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  if (fit) {
    drawFitBackground(
      context,
      source,
      sourceWidth,
      sourceHeight,
      targetWidth,
      targetHeight,
      fillMode,
      transform.fillColor,
      rect,
    );
    drawFittedImage(context, source, rect, targetWidth, targetHeight, blendBorder);
  } else {
    context.drawImage(source, drawX, drawY, drawWidth, drawHeight);
  }
  context.restore();
}

export function canvasToGbaBmp(canvas) {
  const context = canvas.getContext("2d", { alpha: false, willReadFrequently: true });
  const { width, height } = canvas;
  const rgba = context.getImageData(0, 0, width, height).data;
  const rowBytes = width * 2;
  const padding = (4 - (rowBytes % 4)) % 4;
  const stride = rowBytes + padding;
  const pixelSize = stride * height;
  const bytes = new Uint8Array(54 + pixelSize);
  const view = new DataView(bytes.buffer);

  bytes[0] = 0x42;
  bytes[1] = 0x4d;
  view.setUint32(2, bytes.length, true);
  view.setUint32(10, 54, true);
  view.setUint32(14, 40, true);
  view.setInt32(18, width, true);
  view.setInt32(22, -height, true);
  view.setUint16(26, 1, true);
  view.setUint16(28, 16, true);
  view.setUint32(30, 0, true);
  view.setUint32(34, pixelSize, true);
  view.setInt32(38, 2834, true);
  view.setInt32(42, 2834, true);

  let output = 54;
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const offset = (y * width + x) * 4;
      const red = rgba[offset] >> 3;
      const green = rgba[offset + 1] >> 3;
      const blue = rgba[offset + 2] >> 3;
      const gbaBgr555 = (blue << 10) | (green << 5) | red;
      view.setUint16(output, gbaBgr555, true);
      output += 2;
    }
    output += padding;
  }
  return new Blob([bytes], { type: "image/bmp" });
}

export function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1500);
}
