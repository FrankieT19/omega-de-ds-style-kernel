import {
  chooseDirectory,
  chooseFiles,
  findEntryCaseInsensitive,
  getDirectory,
  listDirectory,
  listFilesRecursive,
  pathExists,
  writeFile,
} from "./filesystem.js";
import {
  canvasToGbaBmp,
  downloadBlob,
  drawCroppedImage,
  imageSourceFromBlob,
} from "./images.js";

const DEFAULT_SYSTEM = "Game Boy Advance";
const DEFAULT_LIBRARY_SYSTEM = "Game Boy Color";
const GBA_LIBRARY_URL = new URL("../data/gba-library.json", import.meta.url);
const LIBRARY_TRANSFORMS_KEY = "ds-style-manager-library-transforms-v1";
const LIBRARY_FIT_KEY = "ds-style-manager-library-fit-v1";
const SINGLE_FIT_KEY = "ds-style-manager-single-fit-v1";
const FIT_FILL_MODE_KEY = "ds-style-manager-fit-fill-mode-v1";
const FIT_FILL_COLOR_KEY = "ds-style-manager-fit-fill-color-v1";
const FIT_FILL_MODES = new Set(["solid", "checkerboard", "blur"]);
const IMAGE_EXTENSIONS = [".png", ".jpg", ".jpeg", ".bmp", ".webp"];
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
const ALL_EMULATED_SYSTEMS = "__all_emulated__";
const EMULATED_SYSTEMS = Object.keys(LIBRETRO_SYSTEMS).filter((system) => system !== DEFAULT_SYSTEM);
const LIBRARY_PREVIEW_TITLES = {
  "Game Boy Advance": "Mario Kart - Super Circuit",
  "Game Boy": "Super Mario Land 2 - 6 Golden Coins",
  "Game Boy Color": "The Legend of Zelda - Oracle of Ages",
  "Nintendo Entertainment System": "Super Mario Bros. 3",
  "Master System": "Sonic The Hedgehog",
  "Game Gear": "Sonic The Hedgehog",
  "SG-1000": "Girl's Garden",
  "PC Engine": "Bonk's Adventure",
  "Neo Geo Pocket": "King of Fighters R-1",
  "Neo Geo Pocket Color": "SNK vs. Capcom - The Match of the Millennium",
  "WonderSwan": "Klonoa - Moonlight Museum",
  "WonderSwan Color": "Final Fantasy",
  "MSX": "Metal Gear",
  "Watara Supervision": "Crystball",
  "ZX Spectrum": "Jet Set Willy",
  "ColecoVision": "Donkey Kong",
  "Arcadia 2001": "Space Attack",
  "Super Cassette Vision": "Dragon Ball - Dragon Daihikyou",
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
const CRC32_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let index = 0; index < table.length; index += 1) {
    let value = index;
    for (let bit = 0; bit < 8; bit += 1) {
      value = (value & 1) ? (0xedb88320 ^ (value >>> 1)) : (value >>> 1);
    }
    table[index] = value >>> 0;
  }
  return table;
})();

function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) value = CRC32_TABLE[(value ^ byte) & 0xff] ^ (value >>> 8);
  return (value ^ 0xffffffff) >>> 0;
}

function zipDateTime(date = new Date()) {
  const year = Math.max(1980, Math.min(2107, date.getFullYear()));
  return {
    date: ((year - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate(),
    time: (date.getHours() << 11) | (date.getMinutes() << 5) | Math.floor(date.getSeconds() / 2),
  };
}

function createZipHeader(length, signature) {
  const bytes = new Uint8Array(length);
  new DataView(bytes.buffer).setUint32(0, signature, true);
  return bytes;
}

export async function buildStoredZip(entries) {
  const encoder = new TextEncoder();
  const chunks = [];
  const centralRecords = [];
  const timestamp = zipDateTime();
  let offset = 0;
  let centralSize = 0;

  for (const entry of entries) {
    const name = String(entry.name || "").replaceAll("\\", "/").replace(/^\/+/, "");
    const nameBytes = encoder.encode(name);
    const data = entry.data instanceof Uint8Array
      ? entry.data
      : new Uint8Array(await entry.data.arrayBuffer());
    const checksum = crc32(data);
    const local = createZipHeader(30 + nameBytes.length, 0x04034b50);
    const localView = new DataView(local.buffer);
    localView.setUint16(4, 20, true);
    localView.setUint16(6, 0x0800, true);
    localView.setUint16(8, 0, true);
    localView.setUint16(10, timestamp.time, true);
    localView.setUint16(12, timestamp.date, true);
    localView.setUint32(14, checksum, true);
    localView.setUint32(18, data.byteLength, true);
    localView.setUint32(22, data.byteLength, true);
    localView.setUint16(26, nameBytes.length, true);
    local.set(nameBytes, 30);

    const central = createZipHeader(46 + nameBytes.length, 0x02014b50);
    const centralView = new DataView(central.buffer);
    centralView.setUint16(4, 20, true);
    centralView.setUint16(6, 20, true);
    centralView.setUint16(8, 0x0800, true);
    centralView.setUint16(10, 0, true);
    centralView.setUint16(12, timestamp.time, true);
    centralView.setUint16(14, timestamp.date, true);
    centralView.setUint32(16, checksum, true);
    centralView.setUint32(20, data.byteLength, true);
    centralView.setUint32(24, data.byteLength, true);
    centralView.setUint16(28, nameBytes.length, true);
    centralView.setUint32(42, offset, true);
    central.set(nameBytes, 46);

    chunks.push(local, data);
    centralRecords.push(central);
    offset += local.byteLength + data.byteLength;
    centralSize += central.byteLength;
  }

  const end = createZipHeader(22, 0x06054b50);
  const endView = new DataView(end.buffer);
  endView.setUint16(8, entries.length, true);
  endView.setUint16(10, entries.length, true);
  endView.setUint32(12, centralSize, true);
  endView.setUint32(16, offset, true);
  return new Blob([...chunks, ...centralRecords, end], { type: "application/zip" });
}

function emptyScanStats(scanned = 0) {
  return { scanned, unmatched: 0, existing: 0, limited: 0, invalid: 0 };
}

function mergeScanStats(target, source) {
  for (const key of Object.keys(target)) target[key] += source[key] || 0;
}

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

function stemOfImage(name) {
  return String(name || "").replace(/\.(png|jpe?g|bmp|webp)$/i, "");
}

function titleWords(value) {
  return artworkMatchKey(value)
    .split(" ")
    .filter((word) => word.length > 1 && !["the", "version", "edition", "game"].includes(word));
}

function titleMatchConfidence(query, candidate) {
  const queryKey = artworkMatchKey(query);
  const candidateKey = artworkMatchKey(candidate);
  if (!queryKey || !candidateKey) return 0;
  if (queryKey === candidateKey) return 1;
  const queryWords = titleWords(query);
  const candidateWords = new Set(titleWords(candidate));
  if (!queryWords.length || !candidateWords.size) return 0;
  const matches = queryWords.filter((word) => candidateWords.has(word)).length;
  const coverage = matches / queryWords.length;
  const reverseCoverage = matches / candidateWords.size;
  return (coverage * 0.72) + (reverseCoverage * 0.28);
}

function paddedSearch(value) {
  const normalized = normalizeSearch(value);
  return normalized ? ` ${normalized} ` : "";
}

function preferredRegionalMatch(candidates, name) {
  const normalized = paddedSearch(name);
  const preferredRegion = normalized.includes(" usa ")
    ? "usa"
    : normalized.includes(" europe ")
      ? "europe"
      : normalized.includes(" japan ")
        ? "japan"
        : "";
  return [...candidates].sort((a, b) => {
    const aName = paddedSearch(a.title || a.name || "");
    const bName = paddedSearch(b.title || b.name || "");
    const aRank = preferredRegion && aName.includes(` ${preferredRegion} `) ? 0 : aName.includes(" usa ") ? 1 : aName.includes(" europe ") ? 2 : 3;
    const bRank = preferredRegion && bName.includes(` ${preferredRegion} `) ? 0 : bName.includes(" usa ") ? 1 : bName.includes(" europe ") ? 2 : 3;
    return aRank - bRank || aName.localeCompare(bName);
  })[0] || null;
}

function gbaEntryRegions(title) {
  const value = paddedSearch(title);
  const regions = [];
  if (value.includes(" usa ")) regions.push("usa");
  if (value.includes(" europe ")) regions.push("europe");
  if (value.includes(" japan ")) regions.push("japan");
  if (!regions.length) regions.push("other");
  return regions;
}

function gbaRegionAllowed(title, allowedRegions) {
  const allowed = new Set(allowedRegions || []);
  if (!allowed.size) return false;
  const regions = gbaEntryRegions(title);
  return regions.some((region) => allowed.has(region));
}

function gbaEntryRank(entry, priorityOrder, allowedRegions) {
  const allowed = new Set(allowedRegions || []);
  const regions = gbaEntryRegions(entry.title).filter((region) => !allowed.size || allowed.has(region));
  const regionOrder = priorityOrder?.length
    ? priorityOrder
    : ["usa", "europe", "japan", "other"];
  const regionRank = Math.min(...regions.map((region) => {
    const index = regionOrder.indexOf(region);
    return index < 0 ? regionOrder.length : index;
  }));
  const preRelease = /\((beta|proto|sample|demo|kiosk|preview|debug)/i.test(entry.title) ? 1 : 0;
  const rerelease = /\((virtual console|switch online|classic mini)/i.test(entry.title) ? 1 : 0;
  return [preRelease, regionRank, rerelease, entry.title.length, entry.title];
}

function compareRanks(a, b) {
  for (let index = 0; index < Math.max(a.length, b.length); index += 1) {
    if (a[index] === b[index]) continue;
    if (typeof a[index] === "number" && typeof b[index] === "number") return a[index] - b[index];
    return String(a[index]).localeCompare(String(b[index]));
  }
  return 0;
}

function canonicalGbaEntries(entries, allowedRegions, priorityOrder) {
  const byCode = new Map();
  for (const entry of entries) {
    if (!gbaRegionAllowed(entry.title, allowedRegions)) continue;
    const current = byCode.get(entry.code);
    if (!current || compareRanks(
      gbaEntryRank(entry, priorityOrder, allowedRegions),
      gbaEntryRank(current, priorityOrder, allowedRegions),
    ) < 0) {
      byCode.set(entry.code, entry);
    }
  }
  return [...byCode.values()].sort((a, b) => a.title.localeCompare(b.title));
}

function buildLocalArtworkIndex(files) {
  const exact = new Map();
  const cleaned = new Map();
  const primary = [];
  const europe = [];
  for (const file of files) {
    const title = stemOfImage(file.name);
    const record = { ...file, title };
    const parts = file.path.split("/").map((part) => part.toLocaleLowerCase());
    const collection = parts.includes("europe") ? europe : primary;
    collection.push(record);
    const exactKey = normalizeSearch(title);
    const cleanedKey = artworkMatchKey(title);
    if (!exact.has(exactKey)) exact.set(exactKey, []);
    if (!cleaned.has(cleanedKey)) cleaned.set(cleanedKey, []);
    exact.get(exactKey).push(record);
    cleaned.get(cleanedKey).push(record);
  }
  return { exact, cleaned, primary, europe };
}

function findLocalArtwork(title, index) {
  const exact = index.exact.get(normalizeSearch(title));
  if (exact?.length) return preferredRegionalMatch(exact, title);
  const cleaned = index.cleaned.get(artworkMatchKey(title));
  if (cleaned?.length) return preferredRegionalMatch(cleaned, title);

  let best = null;
  let bestScore = 0;
  for (const collection of [index.primary, index.europe]) {
    for (const candidate of collection) {
      const score = titleMatchConfidence(title, candidate.title);
      if (score > bestScore) {
        best = candidate;
        bestScore = score;
      }
    }
    if (bestScore >= 0.82) break;
  }
  return bestScore >= 0.72 ? best : null;
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
  const cacheKey = `ds-style-libretro-${repo}-${folder}-v3`;
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
    .filter((item) => item.mode !== "120000")
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
  constructor({ getSdRoot, toast, confirm, onSaved }) {
    this.getSdRoot = getSdRoot;
    this.toast = toast;
    this.confirm = confirm;
    this.onSaved = onSaved;
    this.source = null;
    this.sourceName = "";
    this.libretroIndexes = new Map();
    this.searchToken = 0;
    this.gbaLibraryPromise = null;
    this.artworkFolder = null;
    this.artworkFolderFiles = [];
    this.libraryPreviewSource = null;
    this.libraryPreviewToken = 0;
    this.libraryTransformSystem = DEFAULT_LIBRARY_SYSTEM;
    this.libraryTransforms = this.loadLibraryTransforms();
    this.artworkWorkflow = "single";

    this.wideCanvas = document.querySelector("#wide-preview");
    this.squareCanvas = document.querySelector("#square-preview");
    this.imageInput = document.querySelector("#art-image-input");
    this.previewName = document.querySelector("#art-preview-name");
    this.saveButton = document.querySelector("#save-art-to-sd");
    this.downloadButton = document.querySelector("#download-art");
    this.singleFit = document.querySelector("#art-fit");
    this.singleFit.checked = this.loadSingleFit();
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
    this.saveGbaPackButton = document.querySelector("#save-gba-pack-to-sd");
    this.downloadGbaPackButton = document.querySelector("#download-gba-pack");
    this.scanCancelButton = document.querySelector("#cancel-sd-artwork");
    this.scanStatus = document.querySelector("#sd-artwork-scan-status");
    this.scanSystem = document.querySelector("#scan-art-system");
    this.scanProvider = document.querySelector("#scan-art-provider");
    this.scanSource = document.querySelector("#scan-art-source");
    this.scanSizeMode = document.querySelector("#scan-art-size-mode");
    this.scanAction = document.querySelector("#scan-art-action");
    this.scanGbaRegionInputs = [...document.querySelectorAll("[data-gba-region]")];
    this.scanGbaPriorityList = document.querySelector("#scan-gba-priority-list");
    this.artworkFolderName = document.querySelector("#artwork-folder-name");
    this.libraryFit = document.querySelector("#library-fit");
    this.libraryFit.checked = this.loadLibraryFit();
    this.libraryZoom = document.querySelector("#library-zoom");
    this.libraryPanX = document.querySelector("#library-pan-x");
    this.libraryPanY = document.querySelector("#library-pan-y");
    this.libraryWideCanvas = document.querySelector("#library-wide-preview");
    this.librarySquareCanvas = document.querySelector("#library-square-preview");
    this.libraryPreviewCaption = document.querySelector("#library-preview-caption");
    this.fitFillMode = this.loadFitFillMode();
    this.fitFillColor = this.loadFitFillColor();
    this.fitFillModeInputs = [...document.querySelectorAll("[data-fit-fill-mode]")];
    this.fitFillColorInputs = [...document.querySelectorAll("[data-fit-fill-color]")];
    this.scanRunning = false;
    this.scanCancelRequested = false;

    this.syncFitFillControls();
    this.bind();
    this.refreshFitControls();
    this.refreshGbaPriorityControls();
    this.refreshSystemMode();
    this.loadLibraryTransform(DEFAULT_LIBRARY_SYSTEM);
    this.switchArtworkWorkflow("single");
    this.refreshOutputPreviews();
    this.refreshLibraryUi();
    this.drawEmptyPreviews();
    this.drawEmptyLibraryPreviews();
  }

  bind() {
    document.querySelectorAll("[data-art-workflow]").forEach((button) => {
      button.addEventListener("click", () => this.switchArtworkWorkflow(button.dataset.artWorkflow));
    });

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
    this.singleFit.addEventListener("change", () => {
      this.saveSingleFit();
      this.refreshFitControls();
      this.render();
    });
    for (const input of this.fitFillModeInputs) {
      input.addEventListener("change", () => {
        this.fitFillMode = FIT_FILL_MODES.has(input.value) ? input.value : "solid";
        this.saveFitFillSettings();
        this.syncFitFillControls();
        this.refreshFitControls();
        this.render();
        this.renderLibraryPreview();
      });
    }
    for (const input of this.fitFillColorInputs) {
      input.addEventListener("input", () => {
        this.fitFillColor = /^#[0-9a-f]{6}$/i.test(input.value) ? input.value : "#000000";
        this.saveFitFillSettings();
        this.syncFitFillControls();
        this.render();
        this.renderLibraryPreview();
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
    this.sizeMode.addEventListener("change", () => {
      this.refreshOutputPreviews();
      this.render();
    });
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
    this.scanSystem.addEventListener("change", () => {
      this.saveLibraryTransform();
      this.libraryTransformSystem = this.scanSystem.value === ALL_EMULATED_SYSTEMS ? null : this.scanSystem.value;
      if (this.libraryTransformSystem) this.loadLibraryTransform(this.libraryTransformSystem);
      this.refreshLibraryUi();
      this.refreshLibraryPreview();
      this.updatePackReadyStatus();
    });
    this.scanProvider.addEventListener("change", () => {
      this.refreshLibraryUi();
      this.refreshLibraryPreview();
    });
    this.scanSource.addEventListener("change", () => this.refreshLibraryPreview());
    this.scanSizeMode.addEventListener("change", () => {
      this.refreshOutputPreviews();
      this.renderLibraryPreview();
    });
    this.scanAction.addEventListener("change", () => this.refreshLibraryUi());
    for (const input of this.scanGbaRegionInputs) {
      input.addEventListener("change", () => {
        this.refreshLibraryUi();
        this.updatePackReadyStatus();
      });
    }
    this.scanGbaPriorityList.addEventListener("click", (event) => {
      const button = event.target.closest("[data-region-move]");
      if (button) this.moveGbaPriority(button.closest("[data-gba-priority]"), button.dataset.regionMove);
    });
    document.querySelector("#choose-artwork-folder").addEventListener("click", () => this.chooseArtworkFolder());
    for (const input of [this.libraryZoom, this.libraryPanX, this.libraryPanY]) {
      input.addEventListener("input", () => {
        this.refreshLibraryRangeOutputs();
        this.saveLibraryTransform();
        this.renderLibraryPreview();
      });
    }
    this.libraryFit.addEventListener("change", () => {
      this.saveLibraryFit();
      this.refreshLibraryUi();
      this.renderLibraryPreview();
    });
    document.querySelector("#reset-library-position").addEventListener("click", () => {
      this.libraryZoom.value = "100";
      this.libraryPanX.value = "0";
      this.libraryPanY.value = "0";
      this.refreshLibraryRangeOutputs();
      this.saveLibraryTransform();
      this.renderLibraryPreview();
    });
    this.scanButton.addEventListener("click", () => {
      if (this.artworkWorkflow === "gba-pack") this.addMissingGbaArtwork();
      else this.scanSdLibrary();
    });
    this.saveGbaPackButton.addEventListener("click", () => this.saveGbaPackToSd());
    this.downloadGbaPackButton.addEventListener("click", () => this.downloadGbaPack());
    this.scanCancelButton.addEventListener("click", () => {
      this.scanCancelRequested = true;
      this.scanCancelButton.disabled = true;
      this.scanStatus.textContent = "Stopping after the current artwork...";
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

  switchArtworkWorkflow(workflow) {
    if (!["single", "custom-pack", "gba-pack"].includes(workflow)) return;
    this.artworkWorkflow = workflow;
    document.querySelectorAll("[data-art-workflow]").forEach((button) => {
      const active = button.dataset.artWorkflow === workflow;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-selected", String(active));
    });
    document.querySelectorAll("[data-art-workflow-panel]").forEach((panel) => {
      const panelName = panel.dataset.artWorkflowPanel;
      panel.hidden = workflow === "single" ? panelName !== "single" : panelName !== "pack";
    });

    if (workflow === "single") return;
    this.saveLibraryTransform();
    if (workflow === "gba-pack") {
      this.scanAction.value = "complete_gba";
      this.libraryTransformSystem = DEFAULT_SYSTEM;
      this.loadLibraryTransform(DEFAULT_SYSTEM);
    } else {
      if (this.scanAction.value === "complete_gba") this.scanAction.value = "missing";
      this.libraryTransformSystem = this.scanSystem.value === ALL_EMULATED_SYSTEMS ? null : this.scanSystem.value;
      if (this.libraryTransformSystem) this.loadLibraryTransform(this.libraryTransformSystem);
    }
    this.refreshLibraryUi();
    this.refreshLibraryPreview();
    this.updatePackReadyStatus();
  }

  selectedGbaRegions() {
    return this.scanGbaRegionInputs.filter((input) => input.checked).map((input) => input.value);
  }

  gbaPriorityOrder() {
    return [...this.scanGbaPriorityList.querySelectorAll("[data-gba-priority]")]
      .map((row) => row.dataset.gbaPriority);
  }

  refreshGbaPriorityControls() {
    const rows = [...this.scanGbaPriorityList.querySelectorAll("[data-gba-priority]")];
    rows.forEach((row, index) => {
      row.querySelector(".region-rank").textContent = String(index + 1);
      row.querySelector('[data-region-move="up"]').disabled = index === 0;
      row.querySelector('[data-region-move="down"]').disabled = index === rows.length - 1;
    });
  }

  moveGbaPriority(row, direction) {
    if (!row) return;
    if (direction === "up" && row.previousElementSibling) {
      this.scanGbaPriorityList.insertBefore(row, row.previousElementSibling);
    } else if (direction === "down" && row.nextElementSibling) {
      this.scanGbaPriorityList.insertBefore(row.nextElementSibling, row);
    }
    this.refreshGbaPriorityControls();
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

  loadLibraryTransforms() {
    try {
      const saved = JSON.parse(localStorage.getItem(LIBRARY_TRANSFORMS_KEY) || "{}");
      return saved && typeof saved === "object" ? saved : {};
    } catch {
      return {};
    }
  }

  saveLibraryTransforms() {
    try {
      localStorage.setItem(LIBRARY_TRANSFORMS_KEY, JSON.stringify(this.libraryTransforms));
    } catch {
      // Position memory is optional when browser storage is unavailable.
    }
  }

  loadLibraryFit() {
    try {
      return localStorage.getItem(LIBRARY_FIT_KEY) === "true";
    } catch {
      return false;
    }
  }

  saveLibraryFit() {
    try {
      localStorage.setItem(LIBRARY_FIT_KEY, String(this.libraryFit.checked));
    } catch {
      // Fit mode remains available even when browser storage is unavailable.
    }
  }

  loadSingleFit() {
    try {
      return localStorage.getItem(SINGLE_FIT_KEY) === "true";
    } catch {
      return false;
    }
  }

  saveSingleFit() {
    try {
      localStorage.setItem(SINGLE_FIT_KEY, String(this.singleFit.checked));
    } catch {
      // Fit mode remains available even when browser storage is unavailable.
    }
  }

  loadFitFillMode() {
    try {
      const saved = localStorage.getItem(FIT_FILL_MODE_KEY);
      return FIT_FILL_MODES.has(saved) ? saved : "solid";
    } catch {
      return "solid";
    }
  }

  loadFitFillColor() {
    try {
      const saved = localStorage.getItem(FIT_FILL_COLOR_KEY);
      return /^#[0-9a-f]{6}$/i.test(saved || "") ? saved : "#000000";
    } catch {
      return "#000000";
    }
  }

  saveFitFillSettings() {
    try {
      localStorage.setItem(FIT_FILL_MODE_KEY, this.fitFillMode);
      localStorage.setItem(FIT_FILL_COLOR_KEY, this.fitFillColor);
    } catch {
      // Background choices remain available even when browser storage is unavailable.
    }
  }

  syncFitFillControls() {
    for (const input of this.fitFillModeInputs) input.value = this.fitFillMode;
    for (const input of this.fitFillColorInputs) input.value = this.fitFillColor;
    for (const output of document.querySelectorAll("[data-fit-fill-color-output]")) {
      output.value = this.fitFillColor.toLocaleUpperCase();
    }
  }

  refreshFitControls() {
    const singleFit = this.singleFit.checked;
    document.querySelector("#art-fit-options").hidden = !singleFit;
    document.querySelector("#art-manual-position-controls").hidden = singleFit;
    document.querySelector("#library-fit-options").hidden = !this.libraryFit.checked;
    document.querySelector("#art-fill-color-field").hidden = this.fitFillMode !== "solid";
    document.querySelector("#library-fill-color-field").hidden = this.fitFillMode !== "solid";
  }

  fitFillTransform() {
    return {
      fillMode: this.fitFillMode,
      fillColor: this.fitFillColor,
    };
  }

  currentSingleTransform() {
    return {
      zoom: Number(this.zoom.value) / 100,
      x: Number(this.panX.value) / 100,
      y: Number(this.panY.value) / 100,
      fit: this.singleFit.checked,
      ...this.fitFillTransform(),
    };
  }

  currentLibraryTransform() {
    return {
      zoom: Number(this.libraryZoom.value) / 100,
      x: Number(this.libraryPanX.value) / 100,
      y: Number(this.libraryPanY.value) / 100,
      fit: this.libraryFit.checked,
      ...this.fitFillTransform(),
    };
  }

  libraryTransformForSystem(system) {
    const transform = this.libraryTransforms[system] || { zoom: 100, x: 0, y: 0 };
    return {
      zoom: Number(transform.zoom ?? 100) / 100,
      x: Number(transform.x ?? 0) / 100,
      y: Number(transform.y ?? 0) / 100,
      fit: this.libraryFit.checked,
      ...this.fitFillTransform(),
    };
  }

  saveLibraryTransform() {
    if (!this.libraryTransformSystem) return;
    this.libraryTransforms[this.libraryTransformSystem] = {
      zoom: Number(this.libraryZoom.value),
      x: Number(this.libraryPanX.value),
      y: Number(this.libraryPanY.value),
    };
    this.saveLibraryTransforms();
  }

  loadLibraryTransform(system) {
    const transform = this.libraryTransforms[system] || { zoom: 100, x: 0, y: 0 };
    this.libraryZoom.value = String(transform.zoom ?? 100);
    this.libraryPanX.value = String(transform.x ?? 0);
    this.libraryPanY.value = String(transform.y ?? 0);
    this.refreshLibraryRangeOutputs();
  }

  refreshLibraryRangeOutputs() {
    document.querySelector("#library-zoom-output").value = `${this.libraryZoom.value}%`;
    document.querySelector("#library-pan-x-output").value = this.libraryPanX.value;
    document.querySelector("#library-pan-y-output").value = this.libraryPanY.value;
  }

  refreshOutputPreviews() {
    for (const item of document.querySelectorAll("[data-single-preview-size]")) {
      item.hidden = item.dataset.singlePreviewSize !== this.sizeMode.value;
    }
    for (const item of document.querySelectorAll("[data-library-preview-size]")) {
      item.hidden = item.dataset.libraryPreviewSize !== this.scanSizeMode.value;
    }
  }

  drawEmptyLibraryPreviews() {
    for (const canvas of [this.libraryWideCanvas, this.librarySquareCanvas]) {
      const context = canvas.getContext("2d", { alpha: false });
      context.fillStyle = "#0b0d14";
      context.fillRect(0, 0, canvas.width, canvas.height);
      context.strokeStyle = "#32374a";
      context.lineWidth = 1;
      for (let x = -canvas.height; x < canvas.width; x += 8) {
        context.beginPath();
        context.moveTo(x, 0);
        context.lineTo(x + canvas.height, canvas.height);
        context.stroke();
      }
    }
  }

  renderLibraryPreview() {
    if (!this.libraryPreviewSource) {
      this.drawEmptyLibraryPreviews();
      return;
    }
    const transform = this.currentLibraryTransform();
    drawCroppedImage(this.libraryWideCanvas, this.libraryPreviewSource, transform);
    drawCroppedImage(this.librarySquareCanvas, this.libraryPreviewSource, transform);
  }

  effectiveLibraryPreviewSystem() {
    if (this.artworkWorkflow === "gba-pack") return DEFAULT_SYSTEM;
    return this.scanSystem.value === ALL_EMULATED_SYSTEMS
      ? DEFAULT_LIBRARY_SYSTEM
      : this.scanSystem.value;
  }

  async refreshLibraryPreview() {
    const token = ++this.libraryPreviewToken;
    const system = this.effectiveLibraryPreviewSystem();
    this.libraryPreviewCaption.textContent = "Loading preview artwork...";
    if (this.scanProvider.value === "folder" && !this.artworkFolderFiles?.length) {
      this.libraryPreviewCaption.textContent = "Choose an artwork folder to preview its crop.";
      this.drawEmptyLibraryPreviews();
      return;
    }

    try {
      let blob;
      let label;
      if (this.scanProvider.value === "folder") {
        const file = await this.artworkFolderFiles[0].handle.getFile();
        blob = file;
        label = stemOfImage(file.name);
      } else {
        const folder = this.scanSource.value;
        const names = await this.getLibretroIndex(system, folder);
        const preferred = LIBRARY_PREVIEW_TITLES[system] || "";
        const artName = findLibretroArtworkName(preferred, names) || names[0];
        const response = await fetch(libretroRawUrl(system, folder, artName));
        if (!response.ok) throw new Error(`Artwork request failed (${response.status}).`);
        blob = await response.blob();
        label = displayLibretroName(artName);
      }

      const source = await imageSourceFromBlob(blob);
      if (token !== this.libraryPreviewToken) {
        if (typeof source.close === "function") source.close();
        return;
      }
      if (this.libraryPreviewSource && typeof this.libraryPreviewSource.close === "function") {
        this.libraryPreviewSource.close();
      }
      this.libraryPreviewSource = source;
      this.libraryPreviewCaption.textContent = `${system} preview: ${label}`;
      this.renderLibraryPreview();
    } catch (error) {
      if (token !== this.libraryPreviewToken) return;
      if (this.libraryPreviewSource && typeof this.libraryPreviewSource.close === "function") {
        this.libraryPreviewSource.close();
      }
      this.libraryPreviewSource = null;
      this.libraryPreviewCaption.textContent = error.message || "Preview artwork is unavailable.";
      this.drawEmptyLibraryPreviews();
    }
  }

  refreshLibraryUi() {
    const isFolder = this.scanProvider.value === "folder";
    const isGbaPack = this.artworkWorkflow === "gba-pack";
    const scansAllSystems = !isGbaPack && this.scanSystem.value === ALL_EMULATED_SYSTEMS;
    document.querySelector("#scan-libretro-options").hidden = isFolder;
    document.querySelector("#scan-folder-options").hidden = !isFolder;
    document.querySelector("#scan-system-field").hidden = isGbaPack;
    document.querySelector("#scan-action-field").hidden = isGbaPack;
    document.querySelector("#gba-pack-options").hidden = !isGbaPack;
    document.querySelector("#pack-workflow-badge").textContent = isGbaPack ? "SD card or download" : "SD card";

    if (isGbaPack) {
      this.scanAction.value = "complete_gba";
      document.querySelector("#pack-workflow-title").textContent = "Build a complete GBA pack";
      document.querySelector("#pack-workflow-copy").textContent = "Add only the artwork missing from a connected SD card, or prepare a complete GBA library.";
      document.querySelector("#pack-games-note").textContent = "Choose which GBA regions to use and which release should supply artwork when a header code is shared.";
      document.querySelector("#pack-preview-title").textContent = "Complete GBA artwork pack";
      document.querySelector("#pack-limit-note p").textContent = "Add missing artwork without replacing existing images, save a complete pack to the connected card, or download it as a ZIP.";
    } else {
      if (this.scanAction.value === "complete_gba") this.scanAction.value = "missing";
      document.querySelector("#pack-workflow-title").textContent = "Build an emulated artwork pack";
      document.querySelector("#pack-workflow-copy").textContent = "Scan the connected SD card and add matching artwork for installed emulated games.";
      document.querySelector("#pack-games-note").textContent = "Choose one console or scan every supported emulated system.";
      document.querySelector("#pack-preview-title").textContent = "Emulated artwork pack";
      document.querySelector("#pack-limit-note p").textContent = "Emulated games use exact-name CUSTOM artwork. Each CUSTOM folder supports up to 256 images.";
    }

    const manualPositionControls = document.querySelector("#library-manual-position-controls");
    const resetPosition = document.querySelector("#reset-library-position");
    const positionNote = document.querySelector("#library-position-note");
    const fitsWholeImage = this.libraryFit.checked;
    manualPositionControls.hidden = scansAllSystems || fitsWholeImage;
    resetPosition.hidden = scansAllSystems || fitsWholeImage;
    this.refreshFitControls();
    if (fitsWholeImage) {
      positionNote.textContent = "Every image is centred without cropping.";
    } else if (scansAllSystems) {
      positionNote.textContent = "Each console uses its saved crop. Choose one console above to adjust it.";
    } else {
      positionNote.textContent = "Saved separately for each console.";
    }

    const labels = {
      missing: "Add missing artwork",
      rebuild: "Rebuild installed artwork",
      complete_gba: "Build complete GBA pack",
    };
    this.scanButton.querySelector("span").textContent = isGbaPack
      ? "Add missing GBA artwork"
      : labels[this.scanAction.value] || labels.missing;
    this.scanButton.hidden = false;
    this.saveGbaPackButton.hidden = !isGbaPack;
    this.downloadGbaPackButton.hidden = !isGbaPack;
    this.scanButton.disabled = !this.getSdRoot()
      || this.scanRunning
      || (isFolder && !this.artworkFolder)
      || (isGbaPack && !this.selectedGbaRegions().length);
    this.saveGbaPackButton.disabled = !this.getSdRoot()
      || this.scanRunning
      || !this.selectedGbaRegions().length
      || (isFolder && !this.artworkFolder);
    this.downloadGbaPackButton.disabled = this.scanRunning
      || !this.selectedGbaRegions().length
      || (isFolder && !this.artworkFolder);
    this.scanCancelButton.hidden = !this.scanRunning;
    this.scanCancelButton.disabled = !this.scanRunning || this.scanCancelRequested;
    this.refreshOutputPreviews();
    this.refreshGbaPriorityControls();
  }

  updatePackReadyStatus() {
    if (this.scanRunning) return;
    const connected = Boolean(this.getSdRoot());
    if (this.artworkWorkflow === "gba-pack") {
      this.scanStatus.textContent = this.selectedGbaRegions().length
        ? connected
          ? "Ready to add missing artwork, save a complete pack, or download it."
          : "Connect an SD card to add missing artwork or save directly. Complete-pack download is available now."
        : "Choose at least one game region.";
    } else if (this.scanSystem.value === ALL_EMULATED_SYSTEMS) {
      this.scanStatus.textContent = connected
        ? "Ready to scan all supported emulated games on the connected card."
        : "Connect an SD card to scan installed emulated games.";
    } else {
      this.scanStatus.textContent = connected
        ? `Ready to scan installed ${this.scanSystem.value} games.`
        : `Connect an SD card to scan installed ${this.scanSystem.value} games.`;
    }
  }

  async chooseArtworkFolder() {
    try {
      const handle = await chooseDirectory({ id: "ds-style-artwork-folder", mode: "read" });
      const files = await listFilesRecursive(handle, {
        extensions: IMAGE_EXTENSIONS,
        maxDepth: 12,
        maxFiles: 12000,
      });
      if (!files.length) throw new Error("No supported images were found in that folder.");
      this.artworkFolder = handle;
      this.artworkFolderFiles = files;
      this.artworkFolderName.textContent = `${handle.name} - ${files.length} image${files.length === 1 ? "" : "s"}`;
      this.scanStatus.textContent = this.artworkWorkflow === "gba-pack"
        ? "Artwork folder ready. Add missing artwork, save a complete pack, or download it."
        : "Artwork folder ready. Connect an SD card and choose how it should be installed.";
      await this.refreshLibraryPreview();
      this.refreshLibraryUi();
    } catch (error) {
      if (error.name !== "AbortError") this.toast(error.message, "error");
    }
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
    const transform = this.currentSingleTransform();
    drawCroppedImage(this.wideCanvas, this.source, transform);
    drawCroppedImage(this.squareCanvas, this.source, transform);
  }

  updateButtons() {
    this.downloadButton.disabled = !this.source;
    this.saveButton.disabled = !this.source || !this.getSdRoot();
  }

  onSdChanged() {
    this.updateButtons();
    this.refreshLibraryUi();
    if (!this.scanRunning) {
      const status = this.scanStatus.textContent;
      if (!this.getSdRoot() || status.startsWith("Connect an SD card") || status.startsWith("Ready to ")) {
        this.updatePackReadyStatus();
      }
    }
  }

  selectedSizes() {
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

  async getGbaLibrary() {
    if (!this.gbaLibraryPromise) {
      this.gbaLibraryPromise = fetch(GBA_LIBRARY_URL)
        .then((response) => {
          if (!response.ok) throw new Error(`GBA library request failed (${response.status}).`);
          return response.json();
        })
        .then((entries) => {
          const byCode = new Map();
          for (const entry of entries) {
            const code = validateGbaCode(entry.code);
            const normalized = { title: String(entry.title || "").trim(), code };
            if (!byCode.has(code)) byCode.set(code, []);
            byCode.get(code).push(normalized);
          }
          return { entries, byCode };
        });
    }
    return this.gbaLibraryPromise;
  }

  async existingCustomNames(root, size) {
    const directory = await getDirectory(root, `SYSTEM/${ART_SIZES[size].folder}/CUSTOM`, true);
    const entries = await listDirectory(directory, { filesOnly: true, extension: ".bmp" });
    return new Set(entries.map((entry) => entry.name.toLocaleLowerCase()));
  }

  async installedGames(root, system) {
    const extensions = LIBRETRO_SYSTEMS[system]?.extensions || [];
    const files = await listFilesRecursive(root, {
      extensions,
      maxDepth: 12,
      maxFiles: 12000,
      excludeDirectories: SCAN_EXCLUDED_FOLDERS,
    });
    return files.filter((file) => {
      const lowerName = file.name.toLocaleLowerCase();
      if (lowerName === "ezkernel.bin" || lowerName === "ezkernelnew.bin") return false;
      return systemForFilename(file.name) === system;
    });
  }

  async installedGamesForSystems(root, systems) {
    const requested = new Set(systems);
    const extensions = [...new Set(systems.flatMap((system) => LIBRETRO_SYSTEMS[system]?.extensions || []))];
    const grouped = new Map(systems.map((system) => [system, []]));
    if (!extensions.length) return grouped;

    const files = await listFilesRecursive(root, {
      extensions,
      maxDepth: 12,
      maxFiles: 12000,
      excludeDirectories: SCAN_EXCLUDED_FOLDERS,
    });
    for (const file of files) {
      const lowerName = file.name.toLocaleLowerCase();
      if (lowerName === "ezkernel.bin" || lowerName === "ezkernelnew.bin") continue;
      const system = systemForFilename(file.name);
      if (system && requested.has(system)) grouped.get(system).push(file);
    }
    return grouped;
  }

  bestOfficialGbaEntry(filename, code, library, allowedRegions = [], priorityOrder = []) {
    const allCandidates = library.byCode.get(code) || [];
    const candidates = allowedRegions.length
      ? allCandidates.filter((entry) => gbaRegionAllowed(entry.title, allowedRegions))
      : allCandidates;
    let best = null;
    let bestRank = null;
    const title = stemOfFilename(filename);
    for (const entry of candidates) {
      const score = titleMatchConfidence(title, entry.title);
      if (score < 0.82) continue;
      const rank = [-score, ...gbaEntryRank(entry, priorityOrder, allowedRegions)];
      if (!bestRank || compareRanks(rank, bestRank) < 0) {
        best = entry;
        bestRank = rank;
      }
    }
    return best;
  }

  async describeInstalledGame(candidate, system, gbaLibrary = null, gbaOptions = {}) {
    const targetName = validateCustomName(stemOfFilename(candidate.name));
    if (system !== DEFAULT_SYSTEM) {
      return {
        candidate,
        identity: { mode: "custom", value: targetName },
        searchTitle: targetName,
      };
    }

    const file = await candidate.handle.getFile();
    const header = await readGbaHeader(file);
    const knownEntries = gbaLibrary.byCode.get(header.code) || [];
    if (gbaOptions.regions?.length
      && knownEntries.length
      && !knownEntries.some((entry) => gbaRegionAllowed(entry.title, gbaOptions.regions))) {
      return null;
    }
    const official = this.bestOfficialGbaEntry(
      candidate.name,
      header.code,
      gbaLibrary,
      gbaOptions.regions,
      gbaOptions.priority,
    );
    return {
      candidate,
      identity: official ? { mode: "gba", value: official.code } : { mode: "custom", value: targetName },
      searchTitle: official?.title || targetName,
    };
  }

  async outputReservations(root, sizes) {
    const customNames = {};
    const customAvailable = {};
    for (const size of sizes) {
      customNames[size] = await this.existingCustomNames(root, size);
      customAvailable[size] = Math.max(0, CUSTOM_LIMIT - customNames[size].size);
    }
    return { customNames, customAvailable, outputs: new Set() };
  }

  async reserveOutputSizes(root, identity, sizes, overwrite, reservations, stats) {
    const outputSizes = [];
    for (const size of sizes) {
      const outputPath = this.outputPath(identity, size);
      const outputKey = outputPath.toLocaleLowerCase();
      if (reservations.outputs.has(outputKey)) continue;

      const exists = overwrite ? false : await pathExists(root, outputPath, "file");
      if (exists && !overwrite) {
        stats.existing += 1;
        continue;
      }

      if (identity.mode === "custom") {
        const filename = `${identity.value}.bmp`.toLocaleLowerCase();
        const alreadyCounted = reservations.customNames[size].has(filename);
        if (!alreadyCounted && reservations.customAvailable[size] <= 0) {
          stats.limited += 1;
          continue;
        }
        if (!alreadyCounted) {
          reservations.customNames[size].add(filename);
          reservations.customAvailable[size] -= 1;
        }
      }

      reservations.outputs.add(outputKey);
      outputSizes.push(size);
    }
    return outputSizes;
  }

  async createInstalledTasks(root, options) {
    const {
      system,
      provider,
      libretroFolder,
      localIndex,
      sizes,
      overwrite,
      files: suppliedFiles,
      reservations: sharedReservations,
      transform,
      gbaRegions = [],
      gbaPriority = [],
    } = options;
    const files = suppliedFiles || await this.installedGames(root, system);
    const stats = emptyScanStats(files.length);
    const tasks = [];
    if (!files.length) return { tasks, stats };
    const reservations = sharedReservations || await this.outputReservations(root, sizes);
    const gbaLibrary = system === DEFAULT_SYSTEM ? await this.getGbaLibrary() : null;
    const libretroIndex = provider === "libretro" ? await this.getLibretroIndex(system, libretroFolder) : null;

    for (let index = 0; index < files.length; index += 1) {
      if (this.scanCancelRequested) break;
      this.scanStatus.textContent = `Matching installed games... ${index + 1}/${files.length}`;
      let game;
      try {
        game = await this.describeInstalledGame(files[index], system, gbaLibrary, {
          regions: gbaRegions,
          priority: gbaPriority,
        });
      } catch {
        stats.invalid += 1;
        continue;
      }
      if (!game) continue;

      let source;
      if (provider === "libretro") {
        const artName = findLibretroArtworkName(game.searchTitle, libretroIndex)
          || findLibretroArtworkName(game.identity.value, libretroIndex);
        if (!artName) {
          stats.unmatched += 1;
          continue;
        }
        source = { kind: "url", url: libretroRawUrl(system, libretroFolder, artName) };
      } else {
        const art = findLocalArtwork(game.searchTitle, localIndex)
          || findLocalArtwork(stemOfFilename(files[index].name), localIndex);
        if (!art) {
          stats.unmatched += 1;
          continue;
        }
        source = { kind: "file", handle: art.handle };
      }

      const outputSizes = await this.reserveOutputSizes(root, game.identity, sizes, overwrite, reservations, stats);
      if (outputSizes.length) {
        tasks.push({
          label: game.searchTitle,
          identity: game.identity,
          sizes: outputSizes,
          source,
          transform: transform || this.libraryTransformForSystem(system),
        });
      }
    }
    return { tasks, stats };
  }

  async createCompleteGbaTasks(root, options) {
    const {
      provider,
      libretroFolder,
      localIndex,
      sizes,
      gbaRegions,
      gbaPriority,
      reservations: sharedReservations,
      transform,
    } = options;
    const library = await this.getGbaLibrary();
    const entries = canonicalGbaEntries(library.entries, gbaRegions, gbaPriority);
    const libretroIndex = provider === "libretro" ? await this.getLibretroIndex(DEFAULT_SYSTEM, libretroFolder) : null;
    const tasks = [];
    const stats = emptyScanStats(entries.length);
    const reservations = root ? (sharedReservations || await this.outputReservations(root, sizes)) : null;

    for (let index = 0; index < entries.length; index += 1) {
      if (this.scanCancelRequested) break;
      if (index % 20 === 0) {
        this.scanStatus.textContent = `Preparing the GBA pack... ${index + 1}/${entries.length}`;
        await new Promise((resolve) => window.setTimeout(resolve, 0));
      }
      const entry = entries[index];
      let source;
      if (provider === "libretro") {
        const artName = findLibretroArtworkName(entry.title, libretroIndex);
        if (!artName) {
          stats.unmatched += 1;
          continue;
        }
        source = { kind: "url", url: libretroRawUrl(DEFAULT_SYSTEM, libretroFolder, artName) };
      } else {
        const art = findLocalArtwork(entry.title, localIndex);
        if (!art) {
          stats.unmatched += 1;
          continue;
        }
        source = { kind: "file", handle: art.handle };
      }
      const identity = { mode: "gba", value: validateGbaCode(entry.code) };
      const outputSizes = root
        ? await this.reserveOutputSizes(root, identity, sizes, true, reservations, stats)
        : [...sizes];
      if (outputSizes.length) {
        tasks.push({
          label: entry.title,
          identity,
          sizes: outputSizes,
          source,
          transform: transform || this.libraryTransformForSystem(DEFAULT_SYSTEM),
        });
      }
    }
    return { tasks, stats };
  }

  async executeArtworkTasks(root, tasks) {
    const result = { completed: 0, savedImages: 0, failed: 0, cancelled: false };
    let cursor = 0;
    const worker = async () => {
      while (cursor < tasks.length && !this.scanCancelRequested) {
        const task = tasks[cursor];
        cursor += 1;
        let source = null;
        try {
          let blob;
          if (task.source.kind === "file") {
            blob = await task.source.handle.getFile();
          } else {
            const response = await fetch(task.source.url);
            if (!response.ok) throw new Error(`Artwork request failed (${response.status}).`);
            blob = await response.blob();
          }
          source = await imageSourceFromBlob(blob);
          for (const size of task.sizes) {
            const canvas = document.createElement("canvas");
            canvas.width = ART_SIZES[size].width;
            canvas.height = ART_SIZES[size].height;
            drawCroppedImage(canvas, source, task.transform || this.currentLibraryTransform());
            await writeFile(root, this.outputPath(task.identity, size), canvasToGbaBmp(canvas));
            result.savedImages += 1;
          }
        } catch {
          result.failed += 1;
        } finally {
          if (source && typeof source.close === "function") source.close();
        }
        result.completed += 1;
        this.scanStatus.textContent = `Adding artwork... ${result.completed}/${tasks.length}`;
      }
    };
    await Promise.all(Array.from({ length: Math.min(4, tasks.length) }, () => worker()));
    result.cancelled = this.scanCancelRequested;
    return result;
  }

  async createArtworkArchiveEntries(tasks) {
    const result = { completed: 0, savedImages: 0, failed: 0, cancelled: false, entries: [] };
    let cursor = 0;
    const worker = async () => {
      while (cursor < tasks.length && !this.scanCancelRequested) {
        const task = tasks[cursor];
        cursor += 1;
        let source = null;
        try {
          let blob;
          if (task.source.kind === "file") {
            blob = await task.source.handle.getFile();
          } else {
            const response = await fetch(task.source.url);
            if (!response.ok) throw new Error(`Artwork request failed (${response.status}).`);
            blob = await response.blob();
          }
          source = await imageSourceFromBlob(blob);
          for (const size of task.sizes) {
            const canvas = document.createElement("canvas");
            canvas.width = ART_SIZES[size].width;
            canvas.height = ART_SIZES[size].height;
            drawCroppedImage(canvas, source, task.transform || this.currentLibraryTransform());
            const bmp = canvasToGbaBmp(canvas);
            result.entries.push({
              name: this.outputPath(task.identity, size),
              data: new Uint8Array(await bmp.arrayBuffer()),
            });
            result.savedImages += 1;
          }
        } catch {
          result.failed += 1;
        } finally {
          if (source && typeof source.close === "function") source.close();
        }
        result.completed += 1;
        this.scanStatus.textContent = `Preparing artwork... ${result.completed}/${tasks.length}`;
      }
    };
    await Promise.all(Array.from({ length: Math.min(4, tasks.length) }, () => worker()));
    result.cancelled = this.scanCancelRequested;
    result.entries.sort((a, b) => a.name.localeCompare(b.name));
    return result;
  }

  async confirmArtworkAction(action, system, sizes) {
    if (action === "missing") return true;
    const sizeLabel = sizes[0];
    if (action === "complete_gba") {
      return this.confirm(
        "Download the complete GBA pack?",
        `DS Style will prepare the selected ${sizeLabel} GBA artwork and download it as a ZIP. A complete pack can take some time.`,
        "Download pack",
      );
    }
    return this.confirm(
      `Rebuild ${system} artwork?`,
      `This will replace existing ${sizeLabel} artwork for the installed ${system} games that can be matched.`,
      "Rebuild artwork",
      { danger: true },
    );
  }

  async scanSdLibrary() {
    const root = this.getSdRoot();
    if (!root) {
      this.toast("Connect an SD card first.", "error");
      return;
    }
    if (this.scanRunning) return;

    const selectedSystem = this.scanSystem.value;
    const systems = selectedSystem === ALL_EMULATED_SYSTEMS
      ? [...EMULATED_SYSTEMS]
      : [selectedSystem];
    const provider = this.scanProvider.value;
    const action = this.scanAction.value;
    const sizes = this.scanSelectedSizes();
    if (provider === "folder" && !this.artworkFolder) {
      this.toast("Choose an artwork folder first.", "error");
      return;
    }
    const systemLabel = selectedSystem === ALL_EMULATED_SYSTEMS
      ? "all installed emulated games"
      : selectedSystem;
    if (!(await this.confirmArtworkAction(action, systemLabel, sizes))) return;

    this.scanRunning = true;
    this.scanCancelRequested = false;
    this.refreshLibraryUi();
    this.scanStatus.textContent = `Scanning ${systemLabel}...`;

    try {
      let localIndex = null;
      if (provider === "folder") {
        localIndex = buildLocalArtworkIndex(this.artworkFolderFiles);
      }
      const commonOptions = {
        provider,
        libretroFolder: this.scanSource.value,
        localIndex,
        sizes,
        overwrite: action === "rebuild",
        gbaRegions: this.selectedGbaRegions(),
        gbaPriority: this.gbaPriorityOrder(),
      };
      const installedBySystem = await this.installedGamesForSystems(root, systems);
      const totalInstalled = [...installedBySystem.values()].reduce((total, files) => total + files.length, 0);
      const prepared = { tasks: [], stats: emptyScanStats() };
      if (totalInstalled) {
        const reservations = await this.outputReservations(root, sizes);
        for (const system of systems) {
          if (this.scanCancelRequested) break;
          const files = installedBySystem.get(system) || [];
          if (!files.length) continue;
          this.scanStatus.textContent = `Matching ${system} artwork...`;
          const result = await this.createInstalledTasks(root, {
            ...commonOptions,
            system,
            files,
            reservations,
            transform: this.libraryTransformForSystem(system),
          });
          prepared.tasks.push(...result.tasks);
          mergeScanStats(prepared.stats, result.stats);
        }
      }

      if (this.scanCancelRequested) {
        this.scanStatus.textContent = "Artwork preparation stopped.";
        return;
      }
      if (!prepared.tasks.length) {
        const details = [];
        if (!prepared.stats.scanned) details.push("No supported games were found on the connected card.");
        if (prepared.stats.existing) details.push("All matched artwork is already present.");
        if (prepared.stats.unmatched) details.push(`${prepared.stats.unmatched} game${prepared.stats.unmatched === 1 ? "" : "s"} had no matching artwork.`);
        if (prepared.stats.limited) details.push("A CUSTOM folder has reached its 256-image limit.");
        this.scanStatus.textContent = details.join(" ") || "No matching games or artwork were found.";
        return;
      }

      const result = await this.executeArtworkTasks(root, prepared.tasks);
      const details = [];
      if (result.cancelled) details.push("Stopped early.");
      details.push(`Added ${result.savedImages} image${result.savedImages === 1 ? "" : "s"} for ${result.completed - result.failed} game${result.completed - result.failed === 1 ? "" : "s"}.`);
      if (prepared.stats.existing) details.push(`${prepared.stats.existing} existing image${prepared.stats.existing === 1 ? " was" : "s were"} kept.`);
      if (prepared.stats.unmatched) details.push(`${prepared.stats.unmatched} had no artwork match.`);
      if (prepared.stats.limited) details.push(`${prepared.stats.limited} could not be added because a CUSTOM folder reached 256 images.`);
      if (prepared.stats.invalid) details.push(`${prepared.stats.invalid} game file${prepared.stats.invalid === 1 ? " was" : "s were"} skipped.`);
      if (result.failed) details.push(`${result.failed} item${result.failed === 1 ? "" : "s"} could not be written.`);
      this.scanStatus.textContent = details.join(" ");
      this.toast(details[0] === "Stopped early." ? details[1] : details[0], result.failed ? "error" : "success");
      if (result.savedImages && this.onSaved) await this.onSaved();
    } catch (error) {
      this.scanStatus.textContent = error.message;
      this.toast(error.message, "error");
    } finally {
      this.scanRunning = false;
      this.scanCancelRequested = false;
      this.onSdChanged();
    }
  }

  async addMissingGbaArtwork() {
    if (this.scanRunning) return;
    const root = this.getSdRoot();
    if (!root) {
      this.toast("Connect an SD card first.", "error");
      return;
    }
    const regions = this.selectedGbaRegions();
    if (!regions.length) {
      this.toast("Choose at least one game region.", "error");
      return;
    }
    const provider = this.scanProvider.value;
    if (provider === "folder" && !this.artworkFolder) {
      this.toast("Choose an artwork folder first.", "error");
      return;
    }

    this.scanRunning = true;
    this.scanCancelRequested = false;
    this.refreshLibraryUi();
    this.scanStatus.textContent = "Scanning installed GBA games...";

    try {
      const sizes = this.scanSelectedSizes();
      const files = await this.installedGames(root, DEFAULT_SYSTEM);
      if (!files.length) {
        this.scanStatus.textContent = "No GBA games were found on the connected card.";
        return;
      }

      const localIndex = provider === "folder" ? buildLocalArtworkIndex(this.artworkFolderFiles) : null;
      const prepared = await this.createInstalledTasks(root, {
        system: DEFAULT_SYSTEM,
        provider,
        libretroFolder: this.scanSource.value,
        localIndex,
        sizes,
        overwrite: false,
        files,
        gbaRegions: regions,
        gbaPriority: this.gbaPriorityOrder(),
        transform: this.libraryTransformForSystem(DEFAULT_SYSTEM),
      });
      if (this.scanCancelRequested) {
        this.scanStatus.textContent = "Artwork preparation stopped.";
        return;
      }
      if (!prepared.tasks.length) {
        const details = [];
        if (prepared.stats.existing) details.push("All matching GBA artwork is already present.");
        if (prepared.stats.unmatched) details.push(`${prepared.stats.unmatched} game${prepared.stats.unmatched === 1 ? "" : "s"} had no matching artwork.`);
        if (prepared.stats.invalid) details.push(`${prepared.stats.invalid} file${prepared.stats.invalid === 1 ? " was" : "s were"} skipped.`);
        this.scanStatus.textContent = details.join(" ") || "No matching GBA artwork was found for the selected regions.";
        return;
      }

      const result = await this.executeArtworkTasks(root, prepared.tasks);
      const details = [];
      if (result.cancelled) details.push("Stopped early.");
      details.push(`Added ${result.savedImages} missing image${result.savedImages === 1 ? "" : "s"}.`);
      if (prepared.stats.existing) details.push(`${prepared.stats.existing} existing image${prepared.stats.existing === 1 ? " was" : "s were"} kept.`);
      if (prepared.stats.unmatched) details.push(`${prepared.stats.unmatched} game${prepared.stats.unmatched === 1 ? "" : "s"} had no artwork match.`);
      if (prepared.stats.invalid) details.push(`${prepared.stats.invalid} file${prepared.stats.invalid === 1 ? " was" : "s were"} skipped.`);
      if (result.failed) details.push(`${result.failed} item${result.failed === 1 ? "" : "s"} could not be written.`);
      this.scanStatus.textContent = details.join(" ");
      this.toast(details[0] === "Stopped early." ? details[1] : details[0], result.failed ? "error" : "success");
      if (result.savedImages && this.onSaved) await this.onSaved();
    } catch (error) {
      this.scanStatus.textContent = error.message;
      this.toast(error.message, "error");
    } finally {
      this.scanRunning = false;
      this.scanCancelRequested = false;
      this.onSdChanged();
    }
  }

  async saveGbaPackToSd() {
    if (this.scanRunning) return;
    const root = this.getSdRoot();
    if (!root) {
      this.toast("Connect an SD card first.", "error");
      return;
    }
    const regions = this.selectedGbaRegions();
    if (!regions.length) {
      this.toast("Choose at least one game region.", "error");
      return;
    }
    const provider = this.scanProvider.value;
    if (provider === "folder" && !this.artworkFolder) {
      this.toast("Choose an artwork folder first.", "error");
      return;
    }
    const sizes = this.scanSelectedSizes();
    const sizeLabel = sizes[0] === "wide" ? "wide" : "square";
    const confirmed = await this.confirm(
      "Save the complete GBA pack?",
      `DS Style will build the selected ${sizeLabel} artwork and save it directly to the connected SD card. Matching artwork will be replaced.`,
      "Save to SD card",
    );
    if (!confirmed) return;

    this.scanRunning = true;
    this.scanCancelRequested = false;
    this.refreshLibraryUi();
    this.scanStatus.textContent = "Preparing the complete GBA artwork pack...";

    try {
      const localIndex = provider === "folder" ? buildLocalArtworkIndex(this.artworkFolderFiles) : null;
      const prepared = await this.createCompleteGbaTasks(root, {
        provider,
        libretroFolder: this.scanSource.value,
        localIndex,
        sizes,
        gbaRegions: regions,
        gbaPriority: this.gbaPriorityOrder(),
        transform: this.libraryTransformForSystem(DEFAULT_SYSTEM),
      });
      if (this.scanCancelRequested) {
        this.scanStatus.textContent = "Artwork preparation stopped.";
        return;
      }
      if (!prepared.tasks.length) {
        this.scanStatus.textContent = prepared.stats.unmatched
          ? "No matching artwork was found for the selected regions."
          : "No GBA releases matched the selected regions.";
        return;
      }

      const result = await this.executeArtworkTasks(root, prepared.tasks);
      const details = [];
      if (result.cancelled) details.push("Stopped early.");
      details.push(`Saved ${result.savedImages} image${result.savedImages === 1 ? "" : "s"} to the SD card.`);
      if (prepared.stats.unmatched) details.push(`${prepared.stats.unmatched} release${prepared.stats.unmatched === 1 ? "" : "s"} had no artwork match.`);
      if (result.failed) details.push(`${result.failed} item${result.failed === 1 ? "" : "s"} could not be written.`);
      this.scanStatus.textContent = details.join(" ");
      this.toast(details[0] === "Stopped early." ? details[1] : details[0], result.failed ? "error" : "success");
      if (result.savedImages && this.onSaved) await this.onSaved();
    } catch (error) {
      this.scanStatus.textContent = error.message;
      this.toast(error.message, "error");
    } finally {
      this.scanRunning = false;
      this.scanCancelRequested = false;
      this.onSdChanged();
    }
  }

  async downloadGbaPack() {
    if (this.scanRunning) return;
    const regions = this.selectedGbaRegions();
    if (!regions.length) {
      this.toast("Choose at least one game region.", "error");
      return;
    }
    const provider = this.scanProvider.value;
    if (provider === "folder" && !this.artworkFolder) {
      this.toast("Choose an artwork folder first.", "error");
      return;
    }
    const sizes = this.scanSelectedSizes();
    if (!(await this.confirmArtworkAction("complete_gba", DEFAULT_SYSTEM, sizes))) return;

    this.scanRunning = true;
    this.scanCancelRequested = false;
    this.refreshLibraryUi();
    this.scanStatus.textContent = "Preparing the complete GBA artwork pack...";

    try {
      const localIndex = provider === "folder" ? buildLocalArtworkIndex(this.artworkFolderFiles) : null;
      const prepared = await this.createCompleteGbaTasks(null, {
        provider,
        libretroFolder: this.scanSource.value,
        localIndex,
        sizes,
        gbaRegions: regions,
        gbaPriority: this.gbaPriorityOrder(),
        transform: this.libraryTransformForSystem(DEFAULT_SYSTEM),
      });
      if (this.scanCancelRequested) {
        this.scanStatus.textContent = "Artwork preparation stopped.";
        return;
      }
      if (!prepared.tasks.length) {
        this.scanStatus.textContent = prepared.stats.unmatched
          ? "No matching artwork was found for the selected regions."
          : "No GBA releases matched the selected regions.";
        return;
      }

      const result = await this.createArtworkArchiveEntries(prepared.tasks);
      if (result.cancelled) {
        this.scanStatus.textContent = "Artwork preparation stopped.";
        return;
      }
      if (!result.entries.length) {
        throw new Error("The artwork pack could not be prepared.");
      }

      this.scanStatus.textContent = "Creating the download...";
      const archive = await buildStoredZip(result.entries);
      downloadBlob(archive, "DS-Style-GBA-Artwork-Pack.zip");
      const details = [`Downloaded ${result.savedImages} image${result.savedImages === 1 ? "" : "s"}.`];
      if (prepared.stats.unmatched) details.push(`${prepared.stats.unmatched} release${prepared.stats.unmatched === 1 ? "" : "s"} had no artwork match.`);
      if (result.failed) details.push(`${result.failed} item${result.failed === 1 ? "" : "s"} could not be prepared.`);
      this.scanStatus.textContent = details.join(" ");
      this.toast(details[0], result.failed ? "error" : "success");
    } catch (error) {
      this.scanStatus.textContent = error.message;
      this.toast(error.message, "error");
    } finally {
      this.scanRunning = false;
      this.scanCancelRequested = false;
      this.refreshLibraryUi();
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
      this.toast(`Saved ${sizes[0]} artwork to the SD card.`, "success");
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
        downloadBlob(blob, `${identity.value}.bmp`);
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
