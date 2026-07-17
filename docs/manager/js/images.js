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

export function loadImageElement(blob) {
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

export function sourceDimensions(source) {
  return {
    width: source.width || source.naturalWidth,
    height: source.height || source.naturalHeight,
  };
}

export function drawCroppedImage(canvas, source, transform = {}) {
  const context = canvas.getContext("2d", { alpha: false, willReadFrequently: true });
  const { width: sourceWidth, height: sourceHeight } = sourceDimensions(source);
  const targetWidth = canvas.width;
  const targetHeight = canvas.height;
  const zoom = Math.max(1, Number(transform.zoom || 1));
  const panX = Math.max(-1, Math.min(1, Number(transform.x || 0)));
  const panY = Math.max(-1, Math.min(1, Number(transform.y || 0)));

  context.save();
  context.fillStyle = "#000";
  context.fillRect(0, 0, targetWidth, targetHeight);
  if (!sourceWidth || !sourceHeight) {
    context.restore();
    return;
  }

  const baseScale = Math.max(targetWidth / sourceWidth, targetHeight / sourceHeight);
  const scale = baseScale * zoom;
  const drawWidth = sourceWidth * scale;
  const drawHeight = sourceHeight * scale;
  const availableX = Math.max(0, drawWidth - targetWidth);
  const availableY = Math.max(0, drawHeight - targetHeight);
  const drawX = -availableX * ((panX + 1) / 2);
  const drawY = -availableY * ((panY + 1) / 2);

  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.drawImage(source, drawX, drawY, drawWidth, drawHeight);
  context.restore();
}

export function drawContainedImage(canvas, source, background = "#000000") {
  const context = canvas.getContext("2d", { alpha: false });
  const { width: sourceWidth, height: sourceHeight } = sourceDimensions(source);
  const scale = Math.min(canvas.width / sourceWidth, canvas.height / sourceHeight);
  const width = sourceWidth * scale;
  const height = sourceHeight * scale;
  const x = (canvas.width - width) / 2;
  const y = (canvas.height - height) / 2;
  context.fillStyle = background;
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.drawImage(source, x, y, width, height);
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

export function canvasToBmp24(canvas) {
  const context = canvas.getContext("2d", { alpha: false, willReadFrequently: true });
  const { width, height } = canvas;
  const rgba = context.getImageData(0, 0, width, height).data;
  const rowBytes = width * 3;
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
  view.setInt32(22, height, true);
  view.setUint16(26, 1, true);
  view.setUint16(28, 24, true);
  view.setUint32(34, pixelSize, true);
  view.setInt32(38, 2834, true);
  view.setInt32(42, 2834, true);

  let output = 54;
  for (let y = height - 1; y >= 0; y -= 1) {
    for (let x = 0; x < width; x += 1) {
      const offset = (y * width + x) * 4;
      bytes[output] = rgba[offset + 2];
      bytes[output + 1] = rgba[offset + 1];
      bytes[output + 2] = rgba[offset];
      output += 3;
    }
    output += padding;
  }
  return new Blob([bytes], { type: "image/bmp" });
}

export async function resizeImageToBmp(blob, width, height) {
  const source = await imageSourceFromBlob(blob);
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d", { alpha: false });
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.drawImage(source, 0, 0, width, height);
  if (typeof source.close === "function") source.close();
  return canvasToBmp24(canvas);
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

export function snapHexToGba(value) {
  const match = /^#?([0-9a-f]{6})$/i.exec(String(value || "").trim());
  if (!match) throw new Error("Enter a six-digit HTML colour.");
  const red = parseInt(match[1].slice(0, 2), 16) >> 3;
  const green = parseInt(match[1].slice(2, 4), 16) >> 3;
  const blue = parseInt(match[1].slice(4, 6), 16) >> 3;
  return {
    red,
    green,
    blue,
    rgb: `RGB(${red}, ${green}, ${blue})`,
    hex: gbaRgbToHex(red, green, blue),
  };
}

export function gbaRgbToHex(red, green, blue) {
  const expand = (value) => Math.round((Math.max(0, Math.min(31, Number(value))) / 31) * 255);
  return `#${[expand(red), expand(green), expand(blue)]
    .map((value) => value.toString(16).padStart(2, "0"))
    .join("")}`;
}

export function parseGbaRgb(value) {
  const match = /RGB\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)/i.exec(String(value || ""));
  if (!match) return null;
  const [red, green, blue] = match.slice(1).map(Number);
  if ([red, green, blue].some((channel) => channel < 0 || channel > 31)) return null;
  return { red, green, blue, rgb: `RGB(${red}, ${green}, ${blue})`, hex: gbaRgbToHex(red, green, blue) };
}
