import {
  chooseDirectory,
  chooseFiles,
  findEntryCaseInsensitive,
  getDirectory,
  writeFile,
} from "./filesystem.js";
import {
  canvasToGbaBmp,
  downloadBlob,
  drawCroppedImage,
  imageSourceFromBlob,
} from "./images.js";

const LIBRETRO_TREE_URL = "https://api.github.com/repos/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/git/trees/master?recursive=1";
const LIBRETRO_RAW_ROOT = "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master";
const CUSTOM_LIMIT = 256;
const ART_SIZES = {
  wide: { width: 120, height: 80, folder: "IMGS" },
  square: { width: 80, height: 80, folder: "IMGS2" },
};

function normalizeSearch(value) {
  return String(value || "")
    .normalize("NFKD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLocaleLowerCase()
    .replace(/[^a-z0-9]+/g, " ")
    .trim();
}

function displayLibretroName(name) {
  return name.replace(/\.png$/i, "").replaceAll("_", " ");
}

function encodeLibretroName(name) {
  return name.split("/").map(encodeURIComponent).join("/");
}

function stemOfFilename(name) {
  const trimmed = String(name || "").trim();
  const dot = trimmed.lastIndexOf(".");
  return dot > 0 ? trimmed.slice(0, dot) : trimmed;
}

function validateCustomName(value) {
  let name = String(value || "").trim();
  if (name.toLocaleLowerCase().endsWith(".bmp")) name = name.slice(0, -4).trimEnd();
  if (!name) throw new Error("Enter the exact file or folder name.");
  if (/[<>:"/\\|?*]/.test(name)) throw new Error('Artwork names cannot contain < > : " / \\ | ? *');
  if (name.length > 96) throw new Error("Artwork names must be 96 characters or fewer.");
  return name;
}

function validateGbaCode(value) {
  const code = String(value || "").toLocaleUpperCase().replace(/[^A-Z0-9]/g, "");
  if (code.length !== 4) throw new Error("The GBA header code must be exactly four letters or numbers.");
  return code;
}

async function readGbaHeader(file) {
  if (file.size < 0xc0) throw new Error("That file is too small to be a GBA ROM.");
  const bytes = new Uint8Array(await file.slice(0, 0xc0).arrayBuffer());
  const code = String.fromCharCode(...bytes.slice(0xac, 0xb0)).replace(/[\0 ]/g, "");
  const title = String.fromCharCode(...bytes.slice(0xa0, 0xac)).replace(/[\0 ]/g, "").trim();
  return { code: validateGbaCode(code), title };
}

async function fetchLibretroIndex(folder) {
  const cacheKey = `ds-style-libretro-${folder}-v1`;
  try {
    const cached = JSON.parse(localStorage.getItem(cacheKey) || "null");
    if (cached && Array.isArray(cached.names) && cached.names.length > 1000 && Date.now() - cached.saved < 7 * 86400000) {
      return cached.names;
    }
  } catch {
    // Ignore malformed browser cache data.
  }

  const response = await fetch(LIBRETRO_TREE_URL, { headers: { Accept: "application/vnd.github+json" } });
  if (!response.ok) throw new Error(`Libretro index request failed (${response.status}).`);
  const payload = await response.json();
  const prefix = `${folder}/`;
  const names = (payload.tree || [])
    .map((item) => item.path || "")
    .filter((path) => path.startsWith(prefix) && path.toLocaleLowerCase().endsWith(".png"))
    .map((path) => path.slice(prefix.length))
    .sort((a, b) => a.localeCompare(b, undefined, { sensitivity: "base", numeric: true }));
  if (!names.length) throw new Error("No Libretro artwork was found.");
  try {
    localStorage.setItem(cacheKey, JSON.stringify({ saved: Date.now(), names }));
  } catch {
    // Private browsing and full storage should not stop a search.
  }
  return names;
}

async function customFileCount(root, folder) {
  try {
    const custom = await getDirectory(root, `SYSTEM/${folder}/CUSTOM`, false);
    let count = 0;
    for await (const [name, handle] of custom.entries()) {
      if (handle.kind === "file" && name.toLocaleLowerCase().endsWith(".bmp")) count += 1;
    }
    return count;
  } catch {
    return 0;
  }
}

export class ArtworkController {
  constructor({ getSdRoot, toast, onSaved }) {
    this.getSdRoot = getSdRoot;
    this.toast = toast;
    this.onSaved = onSaved;
    this.source = null;
    this.sourceName = "";
    this.libretroIndexes = new Map();
    this.searchToken = 0;

    this.wideCanvas = document.querySelector("#wide-preview");
    this.squareCanvas = document.querySelector("#square-preview");
    this.imageInput = document.querySelector("#art-image-input");
    this.previewName = document.querySelector("#art-preview-name");
    this.saveButton = document.querySelector("#save-art-to-sd");
    this.downloadButton = document.querySelector("#download-art");
    this.zoom = document.querySelector("#art-zoom");
    this.panX = document.querySelector("#art-pan-x");
    this.panY = document.querySelector("#art-pan-y");
    this.matchMode = document.querySelector("#art-match-mode");
    this.sizeMode = document.querySelector("#art-size-mode");
    this.customName = document.querySelector("#custom-art-name");
    this.gbaCode = document.querySelector("#gba-art-code");
    this.libretroQuery = document.querySelector("#libretro-query");
    this.libretroSource = document.querySelector("#libretro-source");
    this.libretroResults = document.querySelector("#libretro-results");
    this.libretroStatus = document.querySelector("#libretro-status");

    this.bind();
    this.drawEmptyPreviews();
  }

  bind() {
    document.querySelector("#choose-art-image").addEventListener("click", () => this.imageInput.click());
    this.imageInput.addEventListener("change", () => {
      const file = this.imageInput.files?.[0];
      if (file) this.loadSource(file, file.name);
      this.imageInput.value = "";
    });

    const dropZone = document.querySelector("#art-drop-zone");
    dropZone.addEventListener("click", () => this.imageInput.click());
    dropZone.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        this.imageInput.click();
      }
    });
    for (const eventName of ["dragenter", "dragover"]) {
      dropZone.addEventListener(eventName, (event) => {
        event.preventDefault();
        dropZone.classList.add("is-dragging");
      });
    }
    for (const eventName of ["dragleave", "drop"]) {
      dropZone.addEventListener(eventName, (event) => {
        event.preventDefault();
        dropZone.classList.remove("is-dragging");
      });
    }
    dropZone.addEventListener("drop", (event) => {
      const file = [...event.dataTransfer.files].find((candidate) => candidate.type.startsWith("image/"));
      if (file) this.loadSource(file, file.name);
    });

    document.querySelectorAll("[data-art-source]").forEach((button) => {
      button.addEventListener("click", () => this.switchSourcePanel(button.dataset.artSource));
    });

    for (const input of [this.zoom, this.panX, this.panY]) {
      input.addEventListener("input", () => {
        this.refreshRangeOutputs();
        this.render();
      });
    }
    document.querySelector("#reset-art-position").addEventListener("click", () => {
      this.zoom.value = "100";
      this.panX.value = "0";
      this.panY.value = "0";
      this.refreshRangeOutputs();
      this.render();
    });

    this.matchMode.addEventListener("change", () => this.refreshMatchMode());
    document.querySelector("#pick-art-target-file").addEventListener("click", () => this.pickTargetFile());
    document.querySelector("#pick-art-target-folder").addEventListener("click", () => this.pickTargetFolder());
    document.querySelector("#read-gba-code").addEventListener("click", () => this.pickGbaRom());
    document.querySelector("#libretro-search").addEventListener("click", () => this.searchLibretro());
    this.libretroQuery.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        this.searchLibretro();
      }
    });
    this.libretroSource.addEventListener("change", () => {
      if (this.libretroQuery.value.trim()) this.searchLibretro();
    });
    this.saveButton.addEventListener("click", () => this.saveToSd());
    this.downloadButton.addEventListener("click", () => this.download());
  }

  switchSourcePanel(source) {
    document.querySelectorAll("[data-art-source]").forEach((button) => {
      button.classList.toggle("is-active", button.dataset.artSource === source);
    });
    document.querySelector("#local-art-panel").hidden = source !== "local";
    document.querySelector("#libretro-art-panel").hidden = source !== "libretro";
  }

  refreshRangeOutputs() {
    document.querySelector("#zoom-output").value = `${this.zoom.value}%`;
    document.querySelector("#pan-x-output").value = this.panX.value;
    document.querySelector("#pan-y-output").value = this.panY.value;
  }

  refreshMatchMode() {
    const isGba = this.matchMode.value === "gba";
    document.querySelector("#custom-name-fields").hidden = isGba;
    document.querySelector("#gba-code-fields").hidden = !isGba;
  }

  drawEmptyPreviews() {
    for (const canvas of [this.wideCanvas, this.squareCanvas]) {
      const context = canvas.getContext("2d", { alpha: false });
      context.fillStyle = "#080a11";
      context.fillRect(0, 0, canvas.width, canvas.height);
      context.strokeStyle = "#272d40";
      context.lineWidth = 1;
      for (let x = -canvas.height; x < canvas.width; x += 8) {
        context.beginPath();
        context.moveTo(x, 0);
        context.lineTo(x + canvas.height, canvas.height);
        context.stroke();
      }
    }
  }

  async loadSource(blob, name = "Artwork") {
    try {
      const next = await imageSourceFromBlob(blob);
      if (this.source && typeof this.source.close === "function") this.source.close();
      this.source = next;
      this.sourceName = name;
      this.previewName.textContent = name;
      this.render();
      this.updateButtons();
    } catch (error) {
      this.toast(error.message, "error");
    }
  }

  render() {
    if (!this.source) {
      this.drawEmptyPreviews();
      return;
    }
    const transform = {
      zoom: Number(this.zoom.value) / 100,
      x: Number(this.panX.value) / 100,
      y: Number(this.panY.value) / 100,
    };
    drawCroppedImage(this.wideCanvas, this.source, transform);
    drawCroppedImage(this.squareCanvas, this.source, transform);
  }

  updateButtons() {
    this.downloadButton.disabled = !this.source;
    this.saveButton.disabled = !this.source || !this.getSdRoot();
  }

  onSdChanged() {
    this.updateButtons();
  }

  selectedSizes() {
    if (this.sizeMode.value === "both") return ["wide", "square"];
    return [this.sizeMode.value];
  }

  outputIdentity() {
    if (this.matchMode.value === "gba") return { mode: "gba", value: validateGbaCode(this.gbaCode.value) };
    return { mode: "custom", value: validateCustomName(this.customName.value) };
  }

  async pickTargetFile() {
    try {
      const [handle] = await chooseFiles({
        types: [{ description: "Any game or file", accept: { "application/octet-stream": [".gba", ".gb", ".gbc", ".nes", ".sms", ".gg", ".ngp", ".ngc", ".sav", ".txt", ".bin"] } }],
        excludeAcceptAllOption: false,
      });
      const file = await handle.getFile();
      this.customName.value = stemOfFilename(file.name);
    } catch (error) {
      if (error.name !== "AbortError") this.toast(error.message, "error");
    }
  }

  async pickTargetFolder() {
    try {
      const handle = await chooseDirectory({ id: "ds-style-art-target" });
      this.customName.value = handle.name;
    } catch (error) {
      if (error.name !== "AbortError") this.toast(error.message, "error");
    }
  }

  async pickGbaRom() {
    try {
      const [handle] = await chooseFiles({
        types: [{ description: "Game Boy Advance ROM", accept: { "application/octet-stream": [".gba"] } }],
        excludeAcceptAllOption: false,
      });
      const file = await handle.getFile();
      const header = await readGbaHeader(file);
      this.gbaCode.value = header.code;
      document.querySelector("#gba-code-note").textContent = header.title ? `${header.title} - ${header.code}` : header.code;
      if (!this.customName.value) this.customName.value = stemOfFilename(file.name);
    } catch (error) {
      if (error.name !== "AbortError") this.toast(error.message, "error");
    }
  }

  async searchLibretro() {
    const query = normalizeSearch(this.libretroQuery.value);
    if (query.length < 2) {
      this.libretroStatus.textContent = "Enter at least two characters.";
      return;
    }

    const folder = this.libretroSource.value;
    const token = ++this.searchToken;
    this.libretroStatus.textContent = "Loading the Libretro index...";
    this.libretroResults.replaceChildren();
    try {
      let names = this.libretroIndexes.get(folder);
      if (!names) {
        names = await fetchLibretroIndex(folder);
        this.libretroIndexes.set(folder, names);
      }
      if (token !== this.searchToken) return;

      const queryWords = query.split(" ");
      const scored = [];
      for (const name of names) {
        const normalized = normalizeSearch(displayLibretroName(name));
        if (!queryWords.every((word) => normalized.includes(word))) continue;
        let score = normalized.startsWith(query) ? 0 : 10;
        score += Math.abs(normalized.length - query.length) / 100;
        scored.push({ name, score });
      }
      scored.sort((a, b) => a.score - b.score || a.name.localeCompare(b.name));
      const results = scored.slice(0, 30);
      this.libretroStatus.textContent = results.length ? `${results.length} closest matches` : "No matching artwork found.";
      const fragment = document.createDocumentFragment();
      for (const result of results) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "search-result";
        const displayName = displayLibretroName(result.name);
        button.innerHTML = `<span>${escapeHtml(displayName)}</span><small>Use artwork</small>`;
        button.addEventListener("click", () => this.selectLibretro(folder, result.name));
        fragment.append(button);
      }
      this.libretroResults.append(fragment);
    } catch (error) {
      this.libretroStatus.textContent = error.message;
      this.toast(error.message, "error");
    }
  }

  async selectLibretro(folder, name) {
    this.libretroStatus.textContent = "Downloading artwork...";
    try {
      const url = `${LIBRETRO_RAW_ROOT}/${folder}/${encodeLibretroName(name)}`;
      const response = await fetch(url);
      if (!response.ok) throw new Error(`Artwork request failed (${response.status}).`);
      const blob = await response.blob();
      await this.loadSource(blob, displayLibretroName(name));
      if (!this.customName.value) this.customName.value = displayLibretroName(name).replace(/\s*\([^)]*\)\s*$/, "");
      this.libretroStatus.textContent = "Artwork ready.";
    } catch (error) {
      this.libretroStatus.textContent = error.message;
      this.toast(error.message, "error");
    }
  }

  canvasForSize(size) {
    return size === "wide" ? this.wideCanvas : this.squareCanvas;
  }

  outputFilename(identity, size) {
    if (identity.mode === "custom") return `${identity.value}.bmp`;
    return `${identity.value}.bmp`;
  }

  outputPath(identity, size) {
    const { folder } = ART_SIZES[size];
    if (identity.mode === "custom") return `SYSTEM/${folder}/CUSTOM/${identity.value}.bmp`;
    return `SYSTEM/${folder}/${identity.value[0]}/${identity.value[1]}/${identity.value}.bmp`;
  }

  async checkCustomLimit(root, identity, size) {
    if (identity.mode !== "custom") return;
    const { folder } = ART_SIZES[size];
    const directory = await getDirectory(root, `SYSTEM/${folder}/CUSTOM`, true);
    const existing = await findEntryCaseInsensitive(directory, `${identity.value}.bmp`, "file");
    if (existing) return;
    const count = await customFileCount(root, folder);
    if (count >= CUSTOM_LIMIT) throw new Error(`${folder}/CUSTOM already contains ${CUSTOM_LIMIT} images.`);
  }

  async saveToSd() {
    const root = this.getSdRoot();
    if (!root) {
      this.toast("Connect an SD card first.", "error");
      return;
    }
    if (!this.source) return;
    try {
      const identity = this.outputIdentity();
      const sizes = this.selectedSizes();
      for (const size of sizes) {
        await this.checkCustomLimit(root, identity, size);
        const blob = canvasToGbaBmp(this.canvasForSize(size));
        await writeFile(root, this.outputPath(identity, size), blob);
      }
      const label = sizes.length === 2 ? "wide and square artwork" : `${sizes[0]} artwork`;
      this.toast(`Saved ${label} to the SD card.`, "success");
      if (this.onSaved) await this.onSaved();
    } catch (error) {
      this.toast(error.message, "error");
    }
  }

  download() {
    if (!this.source) return;
    try {
      const identity = this.outputIdentity();
      const sizes = this.selectedSizes();
      for (const size of sizes) {
        const blob = canvasToGbaBmp(this.canvasForSize(size));
        const suffix = sizes.length === 2 ? `-${ART_SIZES[size].width}x${ART_SIZES[size].height}` : "";
        downloadBlob(blob, `${identity.value}${suffix}.bmp`);
      }
      this.toast("Artwork downloaded.", "success");
    } catch (error) {
      this.toast(error.message, "error");
    }
  }
}

function escapeHtml(value) {
  const element = document.createElement("span");
  element.textContent = value;
  return element.innerHTML;
}
