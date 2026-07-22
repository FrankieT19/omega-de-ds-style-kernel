const ASSET_ROOT = new URL("../assets/preview/", import.meta.url);

const COLOUR_FOLDERS = {
  "Pale Blue": "pale_blue",
  "Light Blue": "light_blue",
  "Blue": "blue",
  "Dark Blue": "dark_blue",
  "Green": "green",
  "Pale Green": "pale_green",
  "Bright Green": "bright_green",
  "Lime": "lime",
  "Yellow": "yellow",
  "Red": "red",
  "Orange": "orange",
  "Brown": "brown",
  "Pink": "pink",
  "Pale Pink": "pale_pink",
  "Magenta": "magenta",
  "Purple": "purple",
};

const ACCENT_RGB5 = {
  "Pale Blue": [10, 14, 17],
  "Light Blue": [5, 19, 25],
  "Blue": [0, 11, 30],
  "Dark Blue": [0, 0, 18],
  "Green": [0, 20, 7],
  "Pale Green": [9, 24, 15],
  "Bright Green": [0, 24, 0],
  "Lime": [18, 26, 0],
  "Yellow": [26, 24, 0],
  "Red": [31, 0, 2],
  "Orange": [31, 18, 0],
  "Brown": [23, 9, 0],
  "Pink": [31, 3, 20],
  "Pale Pink": [26, 14, 26],
  "Magenta": [26, 0, 29],
  "Purple": [17, 0, 26],
};

const START_LAYOUT = {
  thumb: [30, 48, 56, 37],
  title: [93, 49, 114, 42],
  lastBox: [25, 43, 190, 47],
  sdBox: [25, 92, 95, 45],
  norBox: [120, 92, 95, 45],
  settingsBox: [111, 145, 18, 11],
  sdText: [42, 108, 60],
  norText: [137, 108, 60],
};

const HORIZONTAL_LAYOUT = {
  selected: [60, 27, 120, 80],
  left: [-5, 47, 60, 40],
  right: [185, 47, 60, 40],
  title: [39, 115, 162, 39],
  heart: [45, 118],
};

const VERTICAL_LAYOUT = {
  selected: [7, 62, 84, 56],
  previous: [25, 24, 48, 32],
  next: [25, 124, 48, 32],
  title: [93, 62, 141, 56],
  heart: [97, 64],
};

const LATIN_GLYPHS = new Map([
  ["À", ["A", "grave"]], ["Á", ["A", "acute"]], ["Â", ["A", "circ"]],
  ["Ã", ["A", "tilde"]], ["Ä", ["A", "diaeresis"]], ["Å", ["A", "ring"]],
  ["à", ["a", "grave"]], ["á", ["a", "acute"]], ["â", ["a", "circ"]],
  ["ã", ["a", "tilde"]], ["ä", ["a", "diaeresis"]], ["å", ["a", "ring"]],
  ["Ç", ["C", "cedilla"]], ["ç", ["c", "cedilla"]],
  ["È", ["E", "grave"]], ["É", ["E", "acute"]], ["Ê", ["E", "circ"]], ["Ë", ["E", "diaeresis"]],
  ["è", ["e", "grave"]], ["é", ["e", "acute"]], ["ê", ["e", "circ"]], ["ë", ["e", "diaeresis"]],
  ["Ì", ["I", "grave"]], ["Í", ["I", "acute"]], ["Î", ["I", "circ"]], ["Ï", ["I", "diaeresis"]],
  ["ì", ["i", "grave"]], ["í", ["i", "acute"]], ["î", ["i", "circ"]], ["ï", ["i", "diaeresis"]],
  ["Ñ", ["N", "tilde"]], ["ñ", ["n", "tilde"]],
  ["Ò", ["O", "grave"]], ["Ó", ["O", "acute"]], ["Ô", ["O", "circ"]],
  ["Õ", ["O", "tilde"]], ["Ö", ["O", "diaeresis"]],
  ["ò", ["o", "grave"]], ["ó", ["o", "acute"]], ["ô", ["o", "circ"]],
  ["õ", ["o", "tilde"]], ["ö", ["o", "diaeresis"]],
  ["Ù", ["U", "grave"]], ["Ú", ["U", "acute"]], ["Û", ["U", "circ"]], ["Ü", ["U", "diaeresis"]],
  ["ù", ["u", "grave"]], ["ú", ["u", "acute"]], ["û", ["u", "circ"]], ["ü", ["u", "diaeresis"]],
  ["Ý", ["Y", "acute"]], ["ý", ["y", "acute"]], ["ÿ", ["y", "diaeresis"]],
  ["Ğ", ["G", "breve"]], ["ğ", ["g", "breve"]], ["İ", ["I", "dot"]],
  ["Ş", ["S", "cedilla"]], ["ş", ["s", "cedilla"]],
]);

const SPECIAL_GLYPHS = {
  "ı": [0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00],
  "ß": [0x00, 0x00, 0x70, 0x88, 0x88, 0x90, 0xe0, 0x90, 0x88, 0xf0, 0x00, 0x00],
};

const PREVIEW_CONTEXTS = {
  boot: "auto",
  startScreen: "start",
  startSource: "start",
  clock: "start",
  viewMode: "browse",
  listArt: "browse",
  thumbnails: "browse",
  artBorder: "browse",
  roundedCorners: "browse",
  verticalSide: "browse",
  horizontalSide: "browse",
  hideSystemFiles: "browse",
  listFolders: "browse",
  cleanList: "browse",
};

function rgb5([red, green, blue]) {
  const expand = (value) => Math.round((value / 31) * 255);
  return `rgb(${expand(red)}, ${expand(green)}, ${expand(blue)})`;
}

function imageUrl(path) {
  return new URL(path, ASSET_ROOT).href;
}

function visibleCharacters(text) {
  return [...String(text || "")];
}

function truncateCharacters(text, maxCharacters) {
  const characters = visibleCharacters(text);
  if (characters.length <= maxCharacters) return characters.join("");
  if (maxCharacters <= 3) return characters.slice(0, maxCharacters).join("");
  return `${characters.slice(0, maxCharacters - 3).join("").trimEnd()}...`;
}

function splitTitle(text, maxCharacters, maxLines = 3) {
  const words = String(text || "").trim().split(/\s+/).filter(Boolean);
  const lines = [];
  let current = "";
  for (const word of words) {
    const candidate = current ? `${current} ${word}` : word;
    if (visibleCharacters(candidate).length <= maxCharacters || !current) {
      current = candidate;
      continue;
    }
    lines.push(current);
    current = word;
    if (lines.length === maxLines - 1) break;
  }
  if (current && lines.length < maxLines) lines.push(current);
  const consumed = lines.join(" ");
  if (consumed.length < String(text || "").trim().length && lines.length) {
    lines[lines.length - 1] = truncateCharacters(lines[lines.length - 1], maxCharacters);
  }
  return lines.slice(0, maxLines);
}

export class LauncherPreview {
  constructor(canvas, getPreferences, onDescription = null) {
    this.canvas = canvas;
    this.context = canvas.getContext("2d", { alpha: false });
    this.getPreferences = getPreferences;
    this.onDescription = onDescription;
    this.scene = "auto";
    this.contextScene = null;
    this.contextPreference = null;
    this.renderSequence = 0;
    this.hasRendered = false;
    this.images = new Map();
    this.fontData = null;
    this.languages = {};
    this.ready = this.loadData();
    this.render();
  }

  async loadData() {
    const [fontResponse, languageResponse] = await Promise.all([
      fetch(imageUrl("font.bin")),
      fetch(imageUrl("languages.json")),
    ]);
    if (!fontResponse.ok || !languageResponse.ok) throw new Error("Preview assets are unavailable.");
    this.fontData = new Uint8Array(await fontResponse.arrayBuffer());
    this.languages = await languageResponse.json();
  }

  loadImage(path) {
    if (!this.images.has(path)) {
      this.images.set(path, new Promise((resolve, reject) => {
        const source = new Image();
        source.decoding = "async";
        source.onload = () => resolve(source);
        source.onerror = () => reject(new Error(`Could not load ${path}.`));
        source.src = imageUrl(path);
      }));
    }
    return this.images.get(path);
  }

  setScene(scene) {
    this.scene = ["auto", "start", "browse"].includes(scene) ? scene : "auto";
    this.contextScene = null;
    this.contextPreference = null;
    this.render();
  }

  showPreference(preference) {
    if (this.scene !== "auto") return this.render();
    this.contextPreference = preference;
    const nextScene = PREVIEW_CONTEXTS[preference];
    if (nextScene === "start") this.contextScene = "start";
    else if (nextScene === "browse") this.contextScene = "browse";
    else if (nextScene === "auto") this.contextScene = null;
    this.render();
  }

  effectiveScene(preferences) {
    if (this.scene !== "auto") return this.scene;
    if (this.contextScene) {
      if (this.contextScene === "start" && preferences.startScreen === "Off") return "browse";
      return this.contextScene;
    }
    return preferences.startScreen === "On" && preferences.boot === "Start" ? "start" : "browse";
  }

  async render() {
    const sequence = ++this.renderSequence;
    const preferences = { ...this.getPreferences() };
    if (!this.hasRendered) this.drawLoadingFrame();
    try {
      await this.ready;
      const scene = this.effectiveScene(preferences);
      const mode = this.previewMode(preferences);
      const backgroundName = scene === "start" ? "start" : this.backgroundForMode(mode);
      const themeFolder = preferences.theme === "Dark" ? "dark" : "light";
      const colourFolder = COLOUR_FOLDERS[preferences.colour] || "pale_blue";
      const assets = await Promise.all([
        this.loadImage(`backgrounds/${themeFolder}/${backgroundName}.png`),
        this.loadImage(`topbars/${colourFolder}.png`),
        this.loadImage(`icons/${colourFolder}/folder.png`),
        this.loadImage(`icons/${colourFolder}/gba.png`),
        this.loadImage("artwork/amke-wide.png"),
        this.loadImage("artwork/amke-square.png"),
        this.loadImage("artwork/bmge-wide.png"),
        this.loadImage("artwork/bmge-square.png"),
        this.loadImage("artwork/bpee-wide.png"),
        this.loadImage("artwork/a2ae-square.png"),
      ]);
      if (sequence !== this.renderSequence) return;
      const [background, topbar, folderIcon, gbaIcon, amkeWide, amkeSquare, bmgeWide, bmgeSquare, bpeeWide, a2aeSquare] = assets;
      this.currentAssets = {
        background,
        topbar,
        folderIcon,
        gbaIcon,
        artwork: {
          amke: { wide: amkeWide, square: amkeSquare },
          bmge: { wide: bmgeWide, square: bmgeSquare },
          bpee: { wide: bpeeWide, square: a2aeSquare },
        },
      };
      this.drawBase();
      if (scene === "start") this.drawStart(preferences);
      else this.drawBrowse(preferences, mode);
      this.hasRendered = true;
      this.updateDescription(scene, mode, preferences);
    } catch (error) {
      if (sequence !== this.renderSequence) return;
      this.drawErrorFrame();
    }
  }

  drawLoadingFrame() {
    const context = this.context;
    context.imageSmoothingEnabled = false;
    context.fillStyle = "#0b0b0d";
    context.fillRect(0, 0, 240, 160);
  }

  drawErrorFrame() {
    this.drawLoadingFrame();
    this.drawKernelText("Preview unavailable", 66, 74, "#ffffff");
  }

  drawBase() {
    const context = this.context;
    context.imageSmoothingEnabled = false;
    context.drawImage(this.currentAssets.background, 0, 0, 240, 160);
    context.drawImage(this.currentAssets.topbar, 0, 0, 240, 19);
  }

  backgroundForMode(mode) {
    if (mode === "List" || mode === "List + art") return "sd_list";
    if (mode === "Vertical") return "sd_vertical";
    return "sd_horizontal";
  }

  previewMode(preferences) {
    if (this.contextPreference === "listFolders" && preferences.listFolders === "On") {
      return "List";
    }
    return preferences.viewMode || "Horizontal";
  }

  palette(preferences) {
    return {
      bodyText: preferences.theme === "Dark" ? "#ffffff" : "#000000",
      topbarText: "#ffffff",
      selectedText: "#ffffff",
      accent: rgb5(ACCENT_RGB5[preferences.colour] || ACCENT_RGB5["Pale Blue"]),
      grey: rgb5([16, 16, 16]),
    };
  }

  languagePack(preferences) {
    return this.languages[preferences.language] || this.languages["English (UK)"] || {};
  }

  translated(preferences, key, fallback) {
    return this.languagePack(preferences)[key] || fallback;
  }

  drawTopbarName(preferences, palette) {
    this.drawKernelText(preferences.name || "DS Style", 3, 3, palette.topbarText, 11);
  }

  drawClock(preferences, palette) {
    const clock = preferences.clock === "12 hour" ? "12:34 PM" : "12:34:56";
    this.drawKernelText(clock, 189, 3, palette.topbarText);
  }

  drawBrowserTopbar(preferences, palette, total = 8, selected = 4) {
    this.drawTopbarName(preferences, palette);
    this.drawAlignedText("GAMES", 70, 3, 100, "center", palette.topbarText);
    const count = `${selected}/${total}`;
    this.drawKernelText(count, 235 - this.textWidth(count), 3, palette.topbarText);
  }

  drawStart(preferences) {
    const palette = this.palette(preferences);
    const useFavourite = preferences.startSource === "Favourites";
    const artKey = useFavourite ? "bmge" : "amke";
    const title = useFavourite ? "Mario Golf: Advance Tour" : "Mario Kart: Super Circuit";
    this.drawTopbarName(preferences, palette);
    this.drawClock(preferences, palette);
    this.drawArtworkInBox(artKey, START_LAYOUT.thumb, preferences, {
      rounded: preferences.roundedCorners === "Full",
      border: false,
      selected: true,
    });

    const titleLines = splitTitle(title, 18, 3);
    const titleY = START_LAYOUT.title[1] + Math.max(0, Math.floor((START_LAYOUT.title[3] - titleLines.length * 12) / 2));
    titleLines.forEach((line, index) => {
      this.drawAlignedText(line, START_LAYOUT.title[0], titleY + index * 12, START_LAYOUT.title[2], "center", palette.bodyText);
    });

    this.drawAlignedText(
      this.translated(preferences, "DSTEXT_SD_CARD", "SD Card"),
      START_LAYOUT.sdText[0], START_LAYOUT.sdText[1], START_LAYOUT.sdText[2], "center", palette.bodyText,
    );
    this.drawAlignedText(
      this.translated(preferences, "DSTEXT_NOR_FLASH", "NOR Flash"),
      START_LAYOUT.norText[0], START_LAYOUT.norText[1], START_LAYOUT.norText[2], "center", palette.bodyText,
    );
    this.drawSelectionCorners(START_LAYOUT.lastBox, palette.accent);
  }

  drawBrowse(preferences, mode) {
    const palette = this.palette(preferences);
    if (mode === "List" || mode === "List + art") this.drawList(preferences, palette, mode === "List + art");
    else if (mode === "Vertical") this.drawVertical(preferences, palette);
    else this.drawHorizontal(preferences, palette);
  }

  drawList(preferences, palette, withArtwork) {
    const folderOnly = this.contextPreference === "listFolders" && preferences.listFolders === "On";
    const systemRows = preferences.hideSystemFiles === "Off" ? [{ name: "SYSTEM", type: "folder", meta: "DIR" }] : [];
    const folderRows = [
      { name: "Action", type: "folder", meta: "DIR" },
      { name: "Platformer", type: "folder", meta: "DIR" },
      { name: "Puzzle & Arcade", type: "folder", meta: "DIR" },
      { name: "RPG", type: "folder", meta: "DIR" },
      { name: "Racing", type: "folder", meta: "DIR" },
    ];
    const gameRows = [
      { name: "Mario Kart - Super Circuit (USA).gba", clean: "Mario Kart - Super Circuit", type: "game", meta: "16M" },
      { name: "Mario Golf - Advance Tour (USA).gba", clean: "Mario Golf - Advance Tour", type: "game", meta: "16M" },
      { name: "Pokemon Emerald Version (USA).gba", clean: "Pokemon Emerald Version", type: "game", meta: "16M" },
    ];
    const rows = folderOnly ? [...systemRows, ...folderRows] : withArtwork ? [...gameRows, ...folderRows] : [...systemRows, ...folderRows, ...gameRows];
    const selectedRow = folderOnly ? Math.min(2, rows.length - 1) : withArtwork ? 1 : Math.min(3, rows.length - 1);
    const artRect = withArtwork ? this.listArtworkRect(preferences) : null;

    this.drawBrowserTopbar(preferences, palette, rows.length, selectedRow + 1);
    rows.slice(0, 10).forEach((row, index) => {
      const y = 20 + index * 14;
      const selected = index === selectedRow;
      const textColour = selected ? palette.selectedText : palette.bodyText;
      if (selected) this.context.fillStyle = palette.accent;
      if (selected) this.context.fillRect(17, y, 223, 13);
      this.context.drawImage(row.type === "folder" ? this.currentAssets.folderIcon : this.currentAssets.gbaIcon, 0, y, 16, 14);
      const label = preferences.cleanList === "On" ? (row.clean || row.name) : row.name;
      let maxWidth = preferences.cleanList === "On" ? 216 : 190;
      if (selected && artRect && y < artRect[1] + artRect[3] + 1 && y + 14 > artRect[1] - 1) {
        maxWidth = Math.max(48, artRect[0] - 19 - 2);
      }
      this.drawKernelText(truncateCharacters(label, Math.floor(maxWidth / 6)), 17, y, textColour);
      if (preferences.cleanList !== "On") {
        this.drawKernelText(row.meta, 221, y, textColour);
      }
    });

    if (withArtwork) {
      this.drawArtwork("bmge", artRect[0], artRect[1], artRect[2], artRect[3], preferences, {
        rounded: preferences.roundedCorners !== "Off",
        border: preferences.artBorder !== "Off",
        selected: true,
      });
    }
  }

  listArtworkRect(preferences) {
    const width = preferences.thumbnails === "Box" ? 60 : 90;
    const height = 60;
    const x = 240 - 8 - width;
    let y = 92;
    if (preferences.listArt === "Top") y = 27;
    else if (preferences.listArt === "Center") y = 60;
    return [x, y, width, height];
  }

  drawHorizontal(preferences, palette) {
    this.drawBrowserTopbar(preferences, palette, 11, 4);
    const sideY = preferences.horizontalSide === "Top"
      ? HORIZONTAL_LAYOUT.selected[1]
      : preferences.horizontalSide === "Bottom"
        ? HORIZONTAL_LAYOUT.selected[1] + HORIZONTAL_LAYOUT.selected[3] - HORIZONTAL_LAYOUT.left[3]
        : HORIZONTAL_LAYOUT.left[1];
    this.drawArtworkInBox("amke", [HORIZONTAL_LAYOUT.left[0], sideY, HORIZONTAL_LAYOUT.left[2], HORIZONTAL_LAYOUT.left[3]], preferences, {
      rounded: preferences.roundedCorners !== "Off", border: preferences.artBorder !== "Off", selected: false,
    });
    this.drawArtworkInBox("bpee", [HORIZONTAL_LAYOUT.right[0], sideY, HORIZONTAL_LAYOUT.right[2], HORIZONTAL_LAYOUT.right[3]], preferences, {
      rounded: preferences.roundedCorners !== "Off", border: preferences.artBorder !== "Off", selected: false,
    });
    this.drawArtworkInBox("bmge", HORIZONTAL_LAYOUT.selected, preferences, {
      rounded: preferences.roundedCorners !== "Off", border: preferences.artBorder !== "Off", selected: true,
    });
    this.drawWrappedTitle("Mario Golf: Advance Tour", HORIZONTAL_LAYOUT.title, palette.bodyText);
    this.drawHeart(HORIZONTAL_LAYOUT.heart[0], HORIZONTAL_LAYOUT.heart[1], palette.bodyText);
  }

  drawVertical(preferences, palette) {
    this.drawBrowserTopbar(preferences, palette, 11, 4);
    const box = preferences.thumbnails === "Box";
    const mainVisibleX = box ? VERTICAL_LAYOUT.selected[0] + 14 : VERTICAL_LAYOUT.selected[0];
    const mainVisibleWidth = box ? 56 : VERTICAL_LAYOUT.selected[2];
    const sideVisibleWidth = box ? 32 : VERTICAL_LAYOUT.previous[2];
    const sideOffset = box ? 8 : 0;
    let sideX;
    if (preferences.verticalSide === "Left") sideX = mainVisibleX - sideOffset;
    else if (preferences.verticalSide === "Right") sideX = mainVisibleX + mainVisibleWidth - sideVisibleWidth - sideOffset;
    else sideX = mainVisibleX + Math.floor((mainVisibleWidth - sideVisibleWidth) / 2) - sideOffset;

    this.drawArtworkInBox("amke", [sideX, VERTICAL_LAYOUT.previous[1], VERTICAL_LAYOUT.previous[2], VERTICAL_LAYOUT.previous[3]], preferences, {
      rounded: preferences.roundedCorners !== "Off", border: preferences.artBorder !== "Off", selected: false,
    });
    this.drawArtworkInBox("bpee", [sideX, VERTICAL_LAYOUT.next[1], VERTICAL_LAYOUT.next[2], VERTICAL_LAYOUT.next[3]], preferences, {
      rounded: preferences.roundedCorners !== "Off", border: preferences.artBorder !== "Off", selected: false,
    });
    this.drawArtworkInBox("bmge", VERTICAL_LAYOUT.selected, preferences, {
      rounded: preferences.roundedCorners !== "Off", border: preferences.artBorder !== "Off", selected: true,
    });
    this.drawWrappedTitle("Mario Golf: Advance Tour", VERTICAL_LAYOUT.title, palette.bodyText);
    this.drawHeart(VERTICAL_LAYOUT.heart[0], VERTICAL_LAYOUT.heart[1], palette.bodyText);
  }

  drawWrappedTitle(title, box, colour) {
    const [x, y, width, height] = box;
    const lines = splitTitle(title, Math.max(1, Math.floor((width - 8) / 6)), 3);
    const textY = y + Math.max(2, Math.floor((height - lines.length * 12) / 2));
    lines.forEach((line, index) => this.drawAlignedText(line, x + 4, textY + index * 12, width - 8, "center", colour));
  }

  drawArtworkInBox(key, box, preferences, options) {
    const [boxX, boxY, boxWidth, boxHeight] = box;
    let width = boxWidth;
    let height = boxHeight;
    if (preferences.thumbnails === "Box") {
      width = Math.min(boxWidth, boxHeight);
      height = width;
    }
    const x = boxX + Math.floor((boxWidth - width) / 2);
    const y = boxY + Math.floor((boxHeight - height) / 2);
    this.drawArtwork(key, x, y, width, height, preferences, options);
  }

  drawArtwork(key, x, y, width, height, preferences, options) {
    const style = preferences.thumbnails === "Box" ? "square" : "wide";
    const image = this.currentAssets.artwork[key][style];
    const rounded = Boolean(options.rounded);
    const scaled = document.createElement("canvas");
    scaled.width = width;
    scaled.height = height;
    const scaledContext = scaled.getContext("2d", { alpha: false });
    scaledContext.imageSmoothingEnabled = false;
    scaledContext.drawImage(image, 0, 0, width, height);

    if (!rounded) {
      this.context.drawImage(scaled, x, y);
    } else {
      for (let row = 0; row < height; row += 1) {
        const [start, end] = this.roundedRowSpan(row, width, height);
        if (start < end) this.context.drawImage(scaled, start, row, end - start, 1, x + start, y + row, end - start, 1);
      }
    }

    if (options.border) this.drawArtworkBorder(x, y, width, height, rounded, preferences, options.selected);
  }

  roundedRowSpan(row, width, height) {
    let inset = 0;
    if (row === 0 || row === height - 1) inset = 5;
    else if (row === 1 || row === height - 2) inset = 3;
    else if (row === 2 || row === height - 3) inset = 2;
    else if (row === 3 || row === 4 || row === height - 4 || row === height - 5) inset = 1;
    return [inset, width - inset];
  }

  drawArtworkBorder(x, y, width, height, rounded, preferences, selected) {
    const palette = this.palette(preferences);
    const borderColours = {
      Accent: selected ? palette.accent : palette.grey,
      Black: "#000000",
      Grey: palette.grey,
      White: "#ffffff",
    };
    const colour = borderColours[preferences.artBorder];
    if (!colour) return;
    const context = this.context;
    context.fillStyle = colour;
    if (!rounded) {
      context.fillRect(x - 1, y - 1, width + 2, 1);
      context.fillRect(x - 1, y + height, width + 2, 1);
      context.fillRect(x - 1, y - 1, 1, height + 2);
      context.fillRect(x + width, y - 1, 1, height + 2);
      return;
    }
    context.fillRect(x + 5, y - 1, width - 10, 1);
    context.fillRect(x + 5, y + height, width - 10, 1);
    context.fillRect(x - 1, y + 5, 1, height - 10);
    context.fillRect(x + width, y + 5, 1, height - 10);
    const pixels = [
      [3, 0, 2, 1], [2, 1, 1, 1], [1, 2, 1, 1], [0, 3, 1, 2],
      [width - 5, 0, 2, 1], [width - 3, 1, 1, 1], [width - 2, 2, 1, 1], [width - 1, 3, 1, 2],
      [3, height - 1, 2, 1], [2, height - 2, 1, 1], [1, height - 3, 1, 1], [0, height - 5, 1, 2],
      [width - 5, height - 1, 2, 1], [width - 3, height - 2, 1, 1], [width - 2, height - 3, 1, 1], [width - 1, height - 5, 1, 2],
    ];
    pixels.forEach(([offsetX, offsetY, pixelWidth, pixelHeight]) => context.fillRect(x + offsetX, y + offsetY, pixelWidth, pixelHeight));
  }

  drawSelectionCorners([x, y, width, height], colour) {
    const context = this.context;
    context.fillStyle = colour;
    [[x, y], [x + width - 9, y], [x, y + height - 3], [x + width - 9, y + height - 3]].forEach(([cornerX, cornerY]) => {
      context.fillRect(cornerX, cornerY, 9, 3);
    });
    [[x, y], [x + width - 3, y], [x, y + height - 9], [x + width - 3, y + height - 9]].forEach(([cornerX, cornerY]) => {
      context.fillRect(cornerX, cornerY, 3, 9);
    });
  }

  drawHeart(x, y, colour) {
    const context = this.context;
    context.fillStyle = colour;
    [[1, 0, 2, 1], [4, 0, 2, 1], [0, 1, 7, 2], [1, 3, 5, 1], [2, 4, 3, 1], [3, 5, 1, 1]].forEach(([dx, dy, width, height]) => {
      context.fillRect(x + dx, y + dy, width, height);
    });
  }

  textWidth(text) {
    return visibleCharacters(text).length * 6;
  }

  drawAlignedText(text, x, y, width, align, colour) {
    const fitted = truncateCharacters(text, Math.max(1, Math.floor(width / 6)));
    const textWidth = this.textWidth(fitted);
    let textX = x;
    if (align === "center") textX = x + Math.floor((width - textWidth) / 2);
    else if (align === "right") textX = x + width - textWidth;
    this.drawKernelText(fitted, Math.max(x, textX), y, colour);
  }

  drawKernelText(text, x, y, colour, maxCharacters = 0) {
    const characters = visibleCharacters(text);
    const shown = maxCharacters ? characters.slice(0, maxCharacters) : characters;
    let cursorX = Math.round(x);
    for (const character of shown) {
      this.drawGlyph(character, cursorX, Math.round(y), colour);
      cursorX += 6;
    }
  }

  drawGlyph(character, x, y, colour) {
    const special = SPECIAL_GLYPHS[character];
    if (special) {
      this.drawGlyphRows(special, x, y, colour, 6);
      return;
    }
    const mapped = LATIN_GLYPHS.get(character);
    const base = mapped ? mapped[0] : character.codePointAt(0) < 128 ? character : "?";
    const code = base.charCodeAt(0) & 0xff;
    const rows = this.fontData?.slice(code * 12, code * 12 + 12);
    if (!rows?.length) return;
    this.drawGlyphRows(rows, x, y, colour, 8);
    if (mapped) this.drawAccent(mapped[1], mapped[0], x, y, colour);
  }

  drawGlyphRows(rows, x, y, colour, bits) {
    const context = this.context;
    context.fillStyle = colour;
    rows.forEach((row, rowIndex) => {
      for (let column = 0; column < bits; column += 1) {
        if (row & (0x80 >> column)) context.fillRect(x + column, y + rowIndex, 1, 1);
      }
    });
  }

  drawAccent(accent, base, x, y, colour) {
    const lower = base.toLocaleLowerCase();
    let yOffset = accent === "diaeresis" && "aeiouy".includes(lower) ? 2 : 0;
    if (accent === "breve" && lower === "g") yOffset = 1;
    const accents = {
      acute: [[4, 0], [3, 1]],
      grave: [[2, 0], [3, 1]],
      circ: [[3, 0], [2, 1], [4, 1]],
      tilde: [[2, 0], [4, 0], [1, 1], [3, 1]],
      diaeresis: [[2, 0], [4, 0]],
      ring: [[3, 0], [2, 1], [4, 1], [3, 2]],
      breve: [[1, 0], [5, 0], [2, 1], [3, 1], [4, 1]],
      cedilla: [[3, 10], [2, 11], [3, 11]],
      dot: [[3, 0]],
    };
    this.context.fillStyle = colour;
    (accents[accent] || []).forEach(([pixelX, pixelY]) => this.context.fillRect(x + pixelX, y + pixelY + yOffset, 1, 1));
  }

  updateDescription(scene, mode, preferences) {
    if (!this.onDescription) return;
    const label = scene === "start"
      ? "Start screen"
      : mode === "Vertical"
        ? "Vertical carousel"
        : mode === "Horizontal"
          ? "Horizontal carousel"
          : mode;
    this.onDescription(`${label} · ${preferences.theme} · ${preferences.colour}`);
  }
}
