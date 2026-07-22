const PREVIEW_COLOURS = {
  "Pale Blue": "#7499b4",
  "Light Blue": "#35a8dc",
  "Blue": "#1768e5",
  "Dark Blue": "#28348f",
  "Green": "#31a85b",
  "Pale Green": "#78c99a",
  "Bright Green": "#31c74f",
  "Lime": "#98cd31",
  "Yellow": "#d4bd36",
  "Red": "#e44451",
  "Orange": "#ed9538",
  "Brown": "#a86335",
  "Pink": "#e653a8",
  "Pale Pink": "#d989c9",
  "Magenta": "#c94edc",
  "Purple": "#8e62ce",
};

function roundedPath(context, x, y, width, height, radius) {
  const r = Math.max(0, Math.min(radius, width / 2, height / 2));
  context.beginPath();
  context.moveTo(x + r, y);
  context.lineTo(x + width - r, y);
  context.quadraticCurveTo(x + width, y, x + width, y + r);
  context.lineTo(x + width, y + height - r);
  context.quadraticCurveTo(x + width, y + height, x + width - r, y + height);
  context.lineTo(x + r, y + height);
  context.quadraticCurveTo(x, y + height, x, y + height - r);
  context.lineTo(x, y + r);
  context.quadraticCurveTo(x, y, x + r, y);
  context.closePath();
}

function fitText(context, text, maxWidth) {
  const value = String(text || "");
  if (context.measureText(value).width <= maxWidth) return value;
  let output = value;
  while (output.length && context.measureText(`${output}...`).width > maxWidth) output = output.slice(0, -1);
  return `${output}...`;
}

export class LauncherPreview {
  constructor(canvas, getPreferences) {
    this.canvas = canvas;
    this.context = canvas.getContext("2d", { alpha: false });
    this.getPreferences = getPreferences;
    this.scene = "auto";
  }

  setScene(scene) {
    this.scene = ["auto", "start", "browse"].includes(scene) ? scene : "auto";
    this.render();
  }

  palette(preferences) {
    const dark = preferences.theme === "Dark";
    return {
      dark,
      accent: PREVIEW_COLOURS[preferences.colour] || PREVIEW_COLOURS["Pale Blue"],
      body: dark ? "#15171d" : "#f1f1f0",
      panel: dark ? "#22252d" : "#ffffff",
      line: dark ? "#51545d" : "#a5a5a2",
      text: dark ? "#f5f3f7" : "#1c1d20",
      muted: dark ? "#a4a6ad" : "#686a6e",
    };
  }

  render() {
    const preferences = this.getPreferences();
    const palette = this.palette(preferences);
    const context = this.context;
    context.imageSmoothingEnabled = false;
    context.fillStyle = palette.body;
    context.fillRect(0, 0, 240, 160);
    this.drawTopBar(preferences, palette);

    const showStart = this.scene === "start" || (
      this.scene === "auto" && preferences.startScreen === "On" && preferences.boot === "Start"
    );
    if (showStart) this.drawStart(preferences, palette);
    else this.drawBrowse(preferences, palette);
  }

  drawTopBar(preferences, palette) {
    const context = this.context;
    context.fillStyle = palette.accent;
    context.fillRect(0, 0, 240, 19);
    context.fillStyle = "#fff";
    context.font = "bold 9px monospace";
    context.textBaseline = "middle";
    context.fillText(fitText(context, preferences.name || "DS Style", 105), 7, 10);
    const clock = preferences.clock === "12 hour" ? "12:34 PM" : "12:34:56";
    context.textAlign = "right";
    context.fillText(clock, 233, 10);
    context.textAlign = "left";
  }

  drawStart(preferences, palette) {
    const context = this.context;
    context.font = "10px monospace";
    context.textBaseline = "middle";
    const rounded = preferences.roundedCorners === "Full";
    this.drawArtwork(23, 34, 54, 36, preferences, palette, rounded, true);
    context.fillStyle = palette.text;
    context.fillText("Last played", 87, 50);
    this.drawSelection(20, 77, 94, 28, 13, preferences, palette);
    context.fillStyle = "#fff";
    context.fillText("SD Card", 41, 91);
    context.fillStyle = palette.text;
    context.fillText("NOR Flash", 134, 91);
    context.fillText("Settings", 89, 126);
    context.strokeStyle = palette.line;
    context.strokeRect(122.5, 77.5, 96, 27);
  }

  drawBrowse(preferences, palette) {
    const mode = preferences.viewMode || "Horizontal";
    if (mode === "List" || mode === "List + art") this.drawList(preferences, palette, mode === "List + art");
    else if (mode === "Vertical") this.drawVertical(preferences, palette);
    else this.drawHorizontal(preferences, palette);
  }

  drawList(preferences, palette, withArt) {
    const context = this.context;
    context.font = "9px monospace";
    context.textBaseline = "middle";
    const rows = ["Action", "Platformer", "Puzzle & Arcade", "RPG", "Racing", "Tools"];
    for (let index = 0; index < rows.length; index += 1) {
      const y = 30 + index * 19;
      if (index === 2) {
        context.fillStyle = palette.accent;
        context.fillRect(7, y - 7, withArt ? 143 : 226, 15);
      }
      context.fillStyle = index === 2 ? "#fff" : palette.text;
      context.fillRect(12, y - 4, 7, 7);
      context.fillText(rows[index], 25, y);
      if (!preferences.cleanList && (!withArt || y < 44 || y > 116)) {
        context.textAlign = "right";
        context.fillStyle = index === 2 ? "#fff" : palette.muted;
        context.fillText("DIR", 232, y);
        context.textAlign = "left";
      }
    }
    if (withArt) {
      const y = preferences.listArt === "Top" ? 27 : preferences.listArt === "Center" ? 65 : 103;
      const width = preferences.thumbnails === "Box" ? 48 : 72;
      this.drawArtwork(232 - width, y, width, 48, preferences, palette, true);
    }
  }

  drawHorizontal(preferences, palette) {
    const box = preferences.thumbnails === "Box";
    const mainWidth = box ? 80 : 120;
    const mainX = Math.round((240 - mainWidth) / 2);
    const mainY = 32;
    const sideWidth = box ? 36 : 52;
    const sideHeight = 36;
    const sideY = preferences.horizontalSide === "Top" ? mainY : preferences.horizontalSide === "Bottom" ? mainY + 44 : mainY + 22;
    this.drawArtwork(5, sideY, sideWidth, sideHeight, preferences, palette, true, false, "#d9b35b");
    this.drawArtwork(235 - sideWidth, sideY, sideWidth, sideHeight, preferences, palette, true, false, "#6dbb83");
    this.drawArtwork(mainX, mainY, mainWidth, 80, preferences, palette, true, true);
    this.drawTitleBox(41, 121, 158, 27, "Super Mario Advance", preferences, palette);
  }

  drawVertical(preferences, palette) {
    const box = preferences.thumbnails === "Box";
    const mainWidth = box ? 70 : 94;
    const mainX = preferences.verticalSide === "Left" ? 22 : preferences.verticalSide === "Right" ? 124 : 75;
    const sideX = preferences.verticalSide === "Left" ? mainX : preferences.verticalSide === "Right" ? mainX + mainWidth - 42 : mainX + Math.round((mainWidth - 42) / 2);
    this.drawArtwork(sideX, 25, 42, 31, preferences, palette, true, false, "#d9b35b");
    this.drawArtwork(mainX, 62, mainWidth, 64, preferences, palette, true, true);
    this.drawArtwork(sideX, 132, 42, 23, preferences, palette, true, false, "#6dbb83");
    const titleX = mainX < 70 ? 126 : 8;
    this.drawTitleBox(titleX, 70, 104, 47, "The Minish Cap", preferences, palette);
  }

  drawTitleBox(x, y, width, height, title, preferences, palette) {
    const context = this.context;
    context.fillStyle = palette.panel;
    roundedPath(context, x, y, width, height, 5);
    context.fill();
    context.strokeStyle = palette.accent;
    context.stroke();
    context.fillStyle = palette.text;
    context.font = "9px monospace";
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(fitText(context, title, width - 10), x + width / 2, y + height / 2);
    context.textAlign = "left";
  }

  drawSelection(x, y, width, height, radius, preferences, palette) {
    const context = this.context;
    context.fillStyle = palette.accent;
    roundedPath(context, x, y, width, height, radius);
    context.fill();
  }

  drawArtwork(x, y, width, height, preferences, palette, allowRounded, selected = false, colour = null) {
    const context = this.context;
    const rounded = allowRounded && preferences.roundedCorners !== "Off";
    context.save();
    roundedPath(context, x, y, width, height, rounded ? 5 : 0);
    context.clip();
    const gradient = context.createLinearGradient(x, y, x + width, y + height);
    gradient.addColorStop(0, colour || palette.accent);
    gradient.addColorStop(0.5, colour || "#61a6c8");
    gradient.addColorStop(1, palette.dark ? "#342952" : "#d8e7ec");
    context.fillStyle = gradient;
    context.fillRect(x, y, width, height);
    context.fillStyle = "rgba(255,255,255,.55)";
    context.fillRect(x + Math.round(width * 0.12), y + Math.round(height * 0.16), Math.max(3, Math.round(width * 0.23)), Math.max(3, Math.round(height * 0.5)));
    context.fillStyle = "rgba(20,22,31,.45)";
    context.fillRect(x + Math.round(width * 0.47), y + Math.round(height * 0.34), Math.max(4, Math.round(width * 0.4)), Math.max(3, Math.round(height * 0.38)));
    context.restore();

    if (!selected || preferences.artBorder === "Off") return;
    const borderColours = { Accent: palette.accent, Black: "#08090b", Grey: "#8c8d91", White: "#ffffff" };
    context.strokeStyle = borderColours[preferences.artBorder] || palette.accent;
    context.lineWidth = 1;
    roundedPath(context, x - 0.5, y - 0.5, width + 1, height + 1, rounded ? 5.5 : 0);
    context.stroke();
  }
}
