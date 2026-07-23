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
  const colors = ["#727272", "#b8b8b8"];
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

function drawFitBackground(context, source, width, height, mode, color) {
  if (mode === "checkerboard") {
    fillCheckerboard(context, width, height);
    return;
  }

  context.fillStyle = fitFillColor(color);
  context.fillRect(0, 0, width, height);
  if (mode !== "blur") return;

  const padding = 6;
  context.save();
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.filter = "blur(4px)";
  context.drawImage(source, -padding, -padding, width + (padding * 2), height + (padding * 2));
  context.restore();
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
  const fillMode = ["solid", "checkerboard", "blur"].includes(transform.fillMode)
    ? transform.fillMode
    : "solid";

  context.save();
  context.fillStyle = "#000";
  context.fillRect(0, 0, targetWidth, targetHeight);
  if (!sourceWidth || !sourceHeight) {
    context.restore();
    return;
  }
  if (fit) drawFitBackground(context, source, targetWidth, targetHeight, fillMode, transform.fillColor);

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

  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.drawImage(source, drawX, drawY, drawWidth, drawHeight);
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
