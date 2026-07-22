import {
  chooseDirectory,
  chooseFiles,
  findEntryCaseInsensitive,
  getDirectory,
  listDirectory,
  listFilesRecursive,
  writeFile,
} from "./filesystem.js";
import {
  canvasToGbaBmp,
  downloadBlob,
  drawCroppedImage,
  imageSourceFromBlob,
} from "./images.js";

const DEFAULT_SYSTEM = "Game Boy Advance";
const LIBRETRO_SYSTEMS = {
  "Game Boy Advance": { repo: "Nintendo_-_Game_Boy_Advance", extensions: [".gba", ".agb", ".bin", ".mb"] },
  "Game Boy": { repo: "Nintendo_-_Game_Boy", extensions: [".gb"] },
  "Game Boy Color": { repo: "Nintendo_-_Game_Boy_Color", extensions: [".gbc"] },
  "Nintendo Entertainment System": { repo: "Nintendo_-_Nintendo_Entertainment_System", extensions: [".nes"] },
  "Master System": { repo: "Sega_-_Master_System_-_Mark_III", extensions: [".sms"] },
  "Game Gear": { repo: "Sega_-_Game_Gear", extensions: [".gg"] },
  "SG-1000": { repo: "Sega_-_SG-1000", extensions: [".sg"] },
  "PC Engine": { repo: "NEC_-_PC_Engine_-_TurboGrafx_16", extensions: [".pce"] },
  "Neo Geo Pocket": { repo: "SNK_-_Neo_Geo_Pocket", extensions: [".ngp", ".ngc"] },
  "Neo Geo Pocket Color": { repo: "SNK_-_Neo_Geo_Pocket_Color", extensions: [".ngpc"] },
  "WonderSwan": { repo: "Bandai_-_WonderSwan", extensions: [".ws"] },
  "WonderSwan Color": { repo: "Bandai_-_WonderSwan_Color", extensions: [".wsc"] },
  "MSX": { repo: "Microsoft_-_MSX", extensions: [".rom"] },
  "Watara Supervision": { repo: "Watara_-_Supervision", extensions: [".sv"] },
  "ZX Spectrum": { repo: "Sinclair_-_ZX_Spectrum", extensions: [".z80"] },
  "ColecoVision": { repo: "Coleco_-_ColecoVision", extensions: [".col"] },
  "Arcadia 2001": { repo: "Emerson_-_Arcadia_2001", extensions: [".arc"] },
  "Super Cassette Vision": { repo: "Epoch_-_Super_Cassette_Vision", extensions: [".sc"] },
};
const SYSTEM_BY_EXTENSION = new Map(
  Object.entries(LIBRETRO_SYSTEMS).flatMap(([system, details]) => details.extensions.map((extension) => [extension, system])),
);
const SCAN_EXCLUDED_FOLDERS = [
  "$recycle.bin",
  "system volume information",
  "system",
  "saver",
  "rts",
  "cheat",
  "patch",
  "imgs",
  "imgs2",
  "backup",
  "themes",
  "kernels",
];
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

function libretroTreeUrl(system) {
  const repo = LIBRETRO_SYSTEMS[system]?.repo || LIBRETRO_SYSTEMS[DEFAULT_SYSTEM].repo;
  return `https://api.github.com/repos/libretro-thumbnails/${repo}/git/trees/master?recursive=1`;
}

function libretroRawUrl(system, folder, name) {
  const repo = LIBRETRO_SYSTEMS[system]?.repo || LIBRETRO_SYSTEMS[DEFAULT_SYSTEM].repo;
  return `https://raw.githubusercontent.com/libretro-thumbnails/${repo}/master/${folder}/${encodeLibretroName(name)}`;
}

function systemForFilename(name) {
  const dot = String(name || "").lastIndexOf(".");
  if (dot < 0) return null;
  return SYSTEM_BY_EXTENSION.get(name.slice(dot).toLocaleLowerCase()) || null;
}

function artworkMatchKey(value) {
  return normalizeSearch(String(value || "").replace(/\([^)]*\)|\[[^\]]*\]/g, " "));
}

function findLibretroArtworkName(query, names) {
  const normalizedQuery = normalizeSearch(query);
  const cleanedQuery = artworkMatchKey(query);
  let cleanedMatch = null;
  const candidates = [];
  const queryWords = cleanedQuery.split(" ").filter((word) => word.length > 1);

  for (const name of names) {
    const displayName = displayLibretroName(name);
    if (normalizeSearch(displayName) === normalizedQuery) return name;
    const cleanedName = artworkMatchKey(displayName);
    if (cleanedName === cleanedQuery) {
      cleanedMatch ||= name;
      continue;
    }
    if (queryWords.length < 2) continue;
    const nameWords = new Set(cleanedName.split(" "));
    if (!queryWords.every((word) => nameWords.has(word))) continue;
    const regionRank = /\(USA\b/i.test(displayName) ? 0 : /\(Europe\b/i.test(displayName) ? 1 : /\(Japan\b/i.test(displayName) ? 2 : 3;
    candidates.push({
      name,
      rank: regionRank,
      extraWords: Math.max(0, nameWords.size - queryWords.length),
      lengthDifference: Math.abs(cleanedName.length - cleanedQuery.length),
    });
  }
  if (cleanedMatch) return cleanedMatch;
  candidates.sort((a, b) => a.extraWords - b.extraWords || a.rank - b.rank || a.lengthDifference - b.lengthDifference || a.name.localeCompare(b.name));
  return candidates[0]?.name || null;
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

async function fetchLibretroIndex(system, folder) {
  const repo = LIBRETRO_SYSTEMS[system]?.repo || LIBRETRO_SYSTEMS[DEFAULT_SYSTEM].repo;
  const cacheKey = `ds-style-libretro-${repo}-${folder}-v2`;
  try {
    const cached = JSON.parse(localStorage.getItem(cacheKey) || "null");
    if (cached && Array.isArray(cached.names) && cached.names.length && Date.now() - cached.saved < 7 * 86400000) {
      return cached.names;
    }
  } catch {
    // Ignore malformed browser cache data.
  }

  const response = await fetch(libretroTreeUrl(system), { headers: { Accept: "application/vnd.github+json" } });
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
    this.libretroSystem = document.querySelector("#libretro-system");
    this.libretroSource = document.querySelector("#libretro-source");
    this.libretroResults = document.querySelector("#libretro-results");
    this.libretroStatus = document.querySelector("#libretro-status");
    this.scanButton = document.querySelector("#scan-sd-artwork");
    this.scanStatus = document.querySelector("#sd-artwork-scan-status");
    this.scanSource = document.querySelector("#scan-art-source");
    this.scanSizeMode = document.querySelector("#scan-art-size-mode");
    this.scanIncludeGba = document.querySelector("#scan-include-gba");
    this.scanRunning = false;

    this.bind();
    this.refreshSystemMode();
    this.drawEmptyPreviews();
  }

  bind() {
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
    this.libretroSystem.addEventListener("change", () => {
      this.refreshSystemMode();
      this.libretroStatus.textContent = `Search the public ${this.libretroSystem.value} artwork library.`;
      if (this.libretroQuery.value.trim()) this.searchLibretro();
    });
    this.scanButton.addEventListener("click", () => this.scanSdLibrary());
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

  refreshSystemMode() {
    const gbaOption = this.matchMode.querySelector('option[value="gba"]');
    const isGba = this.libretroSystem.value === DEFAULT_SYSTEM;
    gbaOption.disabled = !isGba;
    if (!isGba && this.matchMode.value === "gba") this.matchMode.value = "custom";
    this.refreshMatchMode();
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
    this.scanButton.disabled = !this.getSdRoot() || this.scanRunning;
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
      const supportedExtensions = [...new Set([
        ...Object.values(LIBRETRO_SYSTEMS).flatMap((system) => system.extensions),
        ".sav",
        ".txt",
      ])];
      const [handle] = await chooseFiles({
        startIn: this.getSdRoot() || undefined,
        types: [{ description: "Any game or file", accept: { "application/octet-stream": supportedExtensions } }],
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
    const system = this.libretroSystem.value;
    const token = ++this.searchToken;
    this.libretroStatus.textContent = "Loading the Libretro index...";
    this.libretroResults.replaceChildren();
    try {
      const indexKey = `${system}|${folder}`;
      let names = this.libretroIndexes.get(indexKey);
      if (!names) {
        names = await fetchLibretroIndex(system, folder);
        this.libretroIndexes.set(indexKey, names);
      }
      if (token !== this.searchToken) return;

      const queryWords = query.split(" ");
      const scored = [];
      for (const name of names) {
        const displayName = displayLibretroName(name);
        const normalized = normalizeSearch(displayName);
        if (!queryWords.every((word) => normalized.includes(word))) continue;
        let score = artworkMatchKey(displayName) === artworkMatchKey(this.libretroQuery.value)
          ? -100
          : normalized.startsWith(query) ? 0 : 10;
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
        button.addEventListener("click", () => this.selectLibretro(system, folder, result.name));
        fragment.append(button);
      }
      this.libretroResults.append(fragment);
    } catch (error) {
      this.libretroStatus.textContent = error.message;
      this.toast(error.message, "error");
    }
  }

  async selectLibretro(system, folder, name) {
    this.libretroStatus.textContent = "Downloading artwork...";
    try {
      const url = libretroRawUrl(system, folder, name);
      const response = await fetch(url);
      if (!response.ok) throw new Error(`Artwork request failed (${response.status}).`);
      const blob = await response.blob();
      const displayName = displayLibretroName(name);
      await this.loadSource(blob, displayName);
      if (!this.customName.value) this.customName.value = displayName;
      this.libretroStatus.textContent = "Artwork ready.";
    } catch (error) {
      this.libretroStatus.textContent = error.message;
      this.toast(error.message, "error");
    }
  }

  scanSelectedSizes() {
    if (this.scanSizeMode.value === "both") return ["wide", "square"];
    return [this.scanSizeMode.value];
  }

  async getLibretroIndex(system, folder) {
    const indexKey = `${system}|${folder}`;
    let names = this.libretroIndexes.get(indexKey);
    if (!names) {
      names = await fetchLibretroIndex(system, folder);
      this.libretroIndexes.set(indexKey, names);
    }
    return names;
  }

  async existingCustomNames(root, size) {
    const directory = await getDirectory(root, `SYSTEM/${ART_SIZES[size].folder}/CUSTOM`, true);
    const entries = await listDirectory(directory, { filesOnly: true, extension: ".bmp" });
    return new Set(entries.map((entry) => entry.name.toLocaleLowerCase()));
  }

  async scanSdLibrary() {
    const root = this.getSdRoot();
    if (!root) {
      this.toast("Connect an SD card first.", "error");
      return;
    }
    if (this.scanRunning) return;

    this.scanRunning = true;
    this.scanButton.disabled = true;
    this.scanStatus.textContent = "Scanning the connected SD card...";
    const folder = this.scanSource.value;
    const sizes = this.scanSelectedSizes();
    const includeGba = this.scanIncludeGba.checked;

    try {
      const files = await listFilesRecursive(root, {
        extensions: [...SYSTEM_BY_EXTENSION.keys()],
        maxDepth: 12,
        maxFiles: 12000,
        excludeDirectories: SCAN_EXCLUDED_FOLDERS,
      });
      const candidates = [];
      const seenNames = new Set();
      let invalidNames = 0;
      for (const file of files) {
        if (["ezkernel.bin", "ezkernelnew.bin"].includes(file.name.toLocaleLowerCase())) continue;
        const system = systemForFilename(file.name);
        if (!system || (!includeGba && system === DEFAULT_SYSTEM)) continue;
        let targetName;
        try {
          targetName = validateCustomName(stemOfFilename(file.name));
        } catch {
          invalidNames += 1;
          continue;
        }
        const key = targetName.toLocaleLowerCase();
        if (seenNames.has(key)) continue;
        seenNames.add(key);
        candidates.push({ ...file, system, targetName });
      }

      if (!candidates.length) {
        this.scanStatus.textContent = includeGba
          ? "No supported game files were found."
          : "No supported non-GBA game files were found.";
        return;
      }

      const existingBySize = {};
      const availableBySize = {};
      for (const size of sizes) {
        existingBySize[size] = await this.existingCustomNames(root, size);
        availableBySize[size] = Math.max(0, CUSTOM_LIMIT - existingBySize[size].size);
      }

      const indexes = new Map();
      const systems = [...new Set(candidates.map((candidate) => candidate.system))];
      for (let index = 0; index < systems.length; index += 1) {
        const system = systems[index];
        this.scanStatus.textContent = `Loading ${system} artwork... ${index + 1}/${systems.length}`;
        indexes.set(system, await this.getLibretroIndex(system, folder));
      }

      const tasks = [];
      let unmatched = 0;
      let existing = 0;
      let limited = 0;
      for (const candidate of candidates) {
        const artName = findLibretroArtworkName(candidate.targetName, indexes.get(candidate.system) || []);
        if (!artName) {
          unmatched += 1;
          continue;
        }
        const outputName = `${candidate.targetName}.bmp`.toLocaleLowerCase();
        const neededSizes = [];
        for (const size of sizes) {
          if (existingBySize[size].has(outputName)) {
            existing += 1;
            continue;
          }
          if (availableBySize[size] <= 0) {
            limited += 1;
            continue;
          }
          availableBySize[size] -= 1;
          existingBySize[size].add(outputName);
          neededSizes.push(size);
        }
        if (neededSizes.length) tasks.push({ ...candidate, artName, sizes: neededSizes });
      }

      if (!tasks.length) {
        const note = unmatched ? `${unmatched} files had no Libretro match.` : "All matched artwork is already present.";
        this.scanStatus.textContent = note;
        return;
      }

      let cursor = 0;
      let completed = 0;
      let savedImages = 0;
      let failed = 0;
      const worker = async () => {
        while (cursor < tasks.length) {
          const task = tasks[cursor];
          cursor += 1;
          try {
            const response = await fetch(libretroRawUrl(task.system, folder, task.artName));
            if (!response.ok) throw new Error(`Artwork request failed (${response.status}).`);
            const source = await imageSourceFromBlob(await response.blob());
            try {
              for (const size of task.sizes) {
                const canvas = document.createElement("canvas");
                canvas.width = ART_SIZES[size].width;
                canvas.height = ART_SIZES[size].height;
                drawCroppedImage(canvas, source, { zoom: 1, x: 0, y: 0 });
                await writeFile(
                  root,
                  `SYSTEM/${ART_SIZES[size].folder}/CUSTOM/${task.targetName}.bmp`,
                  canvasToGbaBmp(canvas),
                );
                savedImages += 1;
              }
            } finally {
              if (typeof source.close === "function") source.close();
            }
          } catch {
            failed += 1;
          }
          completed += 1;
          this.scanStatus.textContent = `Adding artwork... ${completed}/${tasks.length}`;
        }
      };
      await Promise.all(Array.from({ length: Math.min(4, tasks.length) }, () => worker()));

      const details = [`Added ${savedImages} image${savedImages === 1 ? "" : "s"} for ${tasks.length - failed} file${tasks.length - failed === 1 ? "" : "s"}.`];
      if (unmatched) details.push(`${unmatched} had no Libretro match.`);
      if (existing) details.push(`${existing} existing image${existing === 1 ? " was" : "s were"} kept.`);
      if (limited) details.push(`${limited} could not be added because a CUSTOM folder reached 256 images.`);
      if (invalidNames) details.push(`${invalidNames} unsupported filename${invalidNames === 1 ? " was" : "s were"} skipped.`);
      if (failed) details.push(`${failed} download${failed === 1 ? "" : "s"} failed.`);
      this.scanStatus.textContent = details.join(" ");
      this.toast(`SD scan complete. ${details[0]}`, failed ? "error" : "success");
      if (savedImages && this.onSaved) await this.onSaved();
    } catch (error) {
      this.scanStatus.textContent = error.message;
      this.toast(error.message, "error");
    } finally {
      this.scanRunning = false;
      this.onSdChanged();
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
