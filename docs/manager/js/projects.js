import {
  chooseDirectory,
  collectProjectFiles,
  getDirectory,
  getFileHandle,
  listFilesRecursive,
  readText,
  writeFile,
  writeText,
} from "./filesystem.js";
import {
  canvasToBmp24,
  gbaRgbToHex,
  imageSourceFromBlob,
  parseGbaRgb,
  resizeImageToBmp,
  snapHexToGba,
  sourceDimensions,
} from "./images.js";

const PROJECT_CONFIG = "customiser_project.json";
const COLOUR_SCRIPT = "Grit/Build Skin Files.ps1";
const SAMPLE_RATE = 22050;
const SOUND_DEFINITIONS = [
  { file: "accept_raw.h", symbol: "accept_raw", label: "Accept", description: "Button accept / enter", limit: 0.35 },
  { file: "back_raw.h", symbol: "back_raw", label: "Back", description: "Button cancel / back", limit: 0.35 },
  { file: "menu_raw.h", symbol: "menu_raw", label: "Menu", description: "Start / menu actions", limit: 0.35 },
  { file: "move_raw.h", symbol: "move_raw", label: "Move", description: "Cursor movement", limit: 0.35 },
  { file: "tab_raw.h", symbol: "tab_raw", label: "Tab", description: "L / R page changes", limit: 0.35 },
  { file: "startup_raw.h", symbol: "startup_raw", label: "Boot", description: "Splash startup sound", limit: 1.7 },
];
const COLOUR_FIELDS = [
  { key: "Selected", label: "Selected text", note: "Text shown inside a selection" },
  { key: "Text", label: "Text", note: "Normal interface text" },
  { key: "SelectSd", label: "Selection", note: "Selection bars and accents" },
  { key: "SelectNor", label: "NOR selection", note: "NOR selection bars" },
  { key: "TopbarText", label: "Top bar text", note: "Username, folder and clock" },
  { key: "Heart", label: "Favourite heart", note: "Heart icon when explicitly set" },
];

function escapeRegex(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function safeName(value) {
  return String(value || "Project").replace(/[<>:"/\\|?*]/g, "").trim() || "Project";
}

async function isSourceDirectory(handle) {
  try {
    await handle.getDirectoryHandle("source", { create: false });
    const grit = await handle.getDirectoryHandle("Grit", { create: false });
    await grit.getFileHandle("Build Skin Files.ps1", { create: false });
    return true;
  } catch {
    return false;
  }
}

async function findProjectSource(selected) {
  if (await isSourceDirectory(selected)) return { project: selected, source: selected };
  for await (const [, handle] of selected.entries()) {
    if (handle.kind === "directory" && await isSourceDirectory(handle)) {
      return { project: selected, source: handle };
    }
  }
  throw new Error("This does not look like a DS Style Customiser project.");
}

async function readOptionalText(root, path) {
  try {
    return await readText(root, path);
  } catch {
    return "";
  }
}

async function readProjectConfig(project) {
  try {
    const file = await project.getFileHandle(PROJECT_CONFIG, { create: false });
    return JSON.parse(await (await file.getFile()).text());
  } catch {
    return {};
  }
}

async function detectModel(project, source) {
  const config = await readProjectConfig(project);
  if (config.model === "omega_de") return { key: "omega_de", label: "Omega Definitive Edition" };
  if (config.model === "original") return { key: "original", label: "Original Omega" };
  const lower = source.name.toLocaleLowerCase();
  if (lower.includes("omega de") || lower.includes("definitive")) return { key: "omega_de", label: "Omega Definitive Edition" };
  const build = (await readOptionalText(source, "build.bat")).toLocaleLowerCase();
  if (build.includes("ezkernelnew.bin")) return { key: "omega_de", label: "Omega Definitive Edition" };
  return { key: "original", label: "Original Omega" };
}

async function detectVersion(source) {
  const text = await readOptionalText(source, "source/launcher_version.h");
  return /LAUNCHER_VERSION_TEXT\s+"v?([0-9]+(?:\.[0-9]+)*)"/.exec(text)?.[1] || "Unknown";
}

function parseThemeBlocks(script) {
  const themes = [];
  const regex = /"([a-z_]+)"\s*\{\s*return\s*@\{([\s\S]*?)\}\s*\}/g;
  let match;
  while ((match = regex.exec(script))) {
    const values = {};
    const valueRegex = /(\w+)\s*=\s*"?(RGB\(\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\)|LAUNCHER_COLOUR_AUTO)"?/g;
    let valueMatch;
    while ((valueMatch = valueRegex.exec(match[2]))) values[valueMatch[1]] = valueMatch[2];
    themes.push({ folder: match[1], values });
  }
  return themes;
}

function titleCaseFolder(value) {
  return value.split("_").map((part) => part.charAt(0).toLocaleUpperCase() + part.slice(1)).join(" ");
}

function replaceThemeField(script, folder, key, value) {
  const blockPattern = new RegExp(`("${escapeRegex(folder)}"\\s*\\{\\s*return\\s*@\\{)([\\s\\S]*?)(\\}\\s*\\})`);
  const blockMatch = blockPattern.exec(script);
  if (!blockMatch) throw new Error(`Could not find the ${titleCaseFolder(folder)} colour set.`);
  const fieldPattern = new RegExp(`(${escapeRegex(key)}\\s*=\\s*)"?(RGB\\(\\s*\\d+\\s*,\\s*\\d+\\s*,\\s*\\d+\\s*\\)|LAUNCHER_COLOUR_AUTO)"?`);
  if (!fieldPattern.test(blockMatch[2])) return script;
  const body = blockMatch[2].replace(fieldPattern, `$1"${value}"`);
  return script.slice(0, blockMatch.index) + blockMatch[1] + body + blockMatch[3] + script.slice(blockMatch.index + blockMatch[0].length);
}

function parseSoundHeader(text) {
  const body = /\[\]\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?=\s*\{([\s\S]*?)\};/.exec(text)?.[1];
  if (!body) return null;
  let values = [...body.matchAll(/-?\d+/g)].map((match) => Number(match[0]));
  values = values.map((value) => (value < 0 ? value + 256 : value) & 0xff);
  const length = /_raw_len\s*=\s*(\d+)/.exec(text)?.[1];
  if (length) values = values.slice(0, Number(length));
  return new Uint8Array(values);
}

function writeSoundHeader(symbol, bytes) {
  const guard = `DS_STYLE_${symbol.toLocaleUpperCase()}_H`;
  const signed = [...bytes].map((byte) => byte > 127 ? byte - 256 : byte);
  const lines = [
    `#ifndef ${guard}`,
    `#define ${guard}`,
    "",
    `static const signed char ${symbol}[] __attribute__((aligned(4))) = {`,
  ];
  for (let index = 0; index < signed.length; index += 16) {
    lines.push(`    ${signed.slice(index, index + 16).join(", ")},`);
  }
  lines.push("};", `static const unsigned int ${symbol}_len = ${bytes.length};`, "", `#endif /* ${guard} */`, "");
  return lines.join("\n");
}

async function decodeAudioToRaw(file, limit) {
  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (!AudioContextClass) throw new Error("Audio conversion is not supported by this browser.");
  const context = new AudioContextClass();
  try {
    const buffer = await context.decodeAudioData(await file.arrayBuffer());
    const outputLength = Math.min(Math.floor(limit * SAMPLE_RATE), Math.floor(buffer.duration * SAMPLE_RATE));
    if (outputLength <= 0) throw new Error("That audio file did not contain usable sound.");
    const output = new Uint8Array(outputLength);
    for (let outputIndex = 0; outputIndex < outputLength; outputIndex += 1) {
      const sourcePosition = outputIndex * buffer.sampleRate / SAMPLE_RATE;
      const leftIndex = Math.min(buffer.length - 1, Math.floor(sourcePosition));
      const rightIndex = Math.min(buffer.length - 1, leftIndex + 1);
      const fraction = sourcePosition - leftIndex;
      let sample = 0;
      for (let channel = 0; channel < buffer.numberOfChannels; channel += 1) {
        const data = buffer.getChannelData(channel);
        sample += data[leftIndex] * (1 - fraction) + data[rightIndex] * fraction;
      }
      sample /= buffer.numberOfChannels;
      const signed = Math.max(-128, Math.min(127, Math.round(sample * 127)));
      output[outputIndex] = signed & 0xff;
    }
    return output;
  } finally {
    await context.close();
  }
}

async function playRawAudio(bytes) {
  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (!AudioContextClass) throw new Error("Audio playback is not supported by this browser.");
  const context = new AudioContextClass({ sampleRate: SAMPLE_RATE });
  const buffer = context.createBuffer(1, bytes.length, SAMPLE_RATE);
  const channel = buffer.getChannelData(0);
  for (let index = 0; index < bytes.length; index += 1) {
    const signed = bytes[index] > 127 ? bytes[index] - 256 : bytes[index];
    channel[index] = signed / 128;
  }
  const source = context.createBufferSource();
  source.buffer = buffer;
  source.connect(context.destination);
  source.onended = () => context.close();
  source.start();
}

function canvasToPng(canvas) {
  return new Promise((resolve, reject) => {
    canvas.toBlob((blob) => blob ? resolve(blob) : reject(new Error("The PNG could not be created.")), "image/png");
  });
}

export class ProjectController {
  constructor({ toast, onChanged }) {
    this.toast = toast;
    this.onChanged = onChanged;
    this.project = null;
    this.source = null;
    this.model = null;
    this.version = "";
    this.config = {};
    this.colourScript = "";
    this.colourThemes = [];
    this.assets = [];
    this.selectedAsset = null;
    this.assetSnapshots = new Map();
    this.previewUrl = "";
    this.pendingSound = null;

    this.bind();
  }

  bind() {
    document.querySelector("#connect-project").addEventListener("click", () => this.openProject());
    document.querySelectorAll("[data-project-tab]").forEach((button) => {
      button.addEventListener("click", () => this.showTab(button.dataset.projectTab));
    });
    document.querySelector("#project-colour-theme").addEventListener("change", () => this.renderColours());
    document.querySelector("#save-project-colours").addEventListener("click", () => this.saveColours());
    document.querySelector("#asset-search").addEventListener("input", () => this.renderAssetList());
    document.querySelector("#replace-project-asset").addEventListener("click", () => document.querySelector("#project-asset-file").click());
    document.querySelector("#project-asset-file").addEventListener("change", (event) => {
      const file = event.target.files?.[0];
      if (file) this.replaceSelectedAsset(file);
      event.target.value = "";
    });
    document.querySelector("#undo-project-asset").addEventListener("click", () => this.undoSelectedAsset());
    document.querySelector("#project-sound-file").addEventListener("change", (event) => {
      const file = event.target.files?.[0];
      if (file && this.pendingSound) this.replaceSound(this.pendingSound, file);
      event.target.value = "";
    });
  }

  async openProject() {
    try {
      const selected = await chooseDirectory({ id: "ds-style-project" });
      const found = await findProjectSource(selected);
      this.project = found.project;
      this.source = found.source;
      this.config = await readProjectConfig(this.project);
      this.model = await detectModel(this.project, this.source);
      this.version = await detectVersion(this.source);
      await this.loadProjectData();
      this.renderConnection();
      document.querySelector("#project-editor").hidden = false;
      if (this.onChanged) this.onChanged();
      this.toast("Project opened.", "success");
    } catch (error) {
      if (error.name !== "AbortError") this.toast(error.message, "error");
    }
  }

  async loadProjectData() {
    this.colourScript = await readText(this.source, COLOUR_SCRIPT);
    this.colourThemes = parseThemeBlocks(this.colourScript);
    if (!this.colourThemes.length) throw new Error("The project colour definitions could not be read.");
    const themeSelect = document.querySelector("#project-colour-theme");
    themeSelect.replaceChildren(...this.colourThemes.map((theme) => {
      const option = document.createElement("option");
      option.value = theme.folder;
      option.textContent = titleCaseFolder(theme.folder);
      return option;
    }));
    this.renderColours();
    await this.loadAssets();
    await this.renderSounds();
  }

  renderConnection() {
    const title = this.config.name || this.project.name || this.source.name;
    document.querySelector("#project-title").textContent = title;
    document.querySelector("#project-copy").textContent = `DS Style v${this.version} project source: ${this.source.name}`;
    document.querySelector("#project-model-pill").textContent = this.model.label;
    document.querySelector("#project-connection-banner").dataset.state = "connected";
  }

  showTab(tab) {
    document.querySelectorAll("[data-project-tab]").forEach((button) => {
      const active = button.dataset.projectTab === tab;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-selected", String(active));
    });
    document.querySelectorAll("[data-project-pane]").forEach((pane) => {
      pane.hidden = pane.dataset.projectPane !== tab;
    });
  }

  renderColours() {
    const folder = document.querySelector("#project-colour-theme").value || this.colourThemes[0]?.folder;
    const theme = this.colourThemes.find((item) => item.folder === folder);
    const grid = document.querySelector("#project-colour-grid");
    grid.replaceChildren();
    if (!theme) return;

    for (const definition of COLOUR_FIELDS) {
      const parsed = parseGbaRgb(theme.values[definition.key]);
      if (!parsed) continue;
      const row = document.createElement("div");
      row.className = "colour-control";
      row.innerHTML = `
        <input type="color" value="${parsed.hex}" aria-label="${definition.label}">
        <label><strong>${definition.label}</strong><small>${definition.note}</small></label>
        <input type="text" value="${parsed.hex}" maxlength="7" aria-label="${definition.label} HTML colour">
      `;
      const picker = row.querySelector('input[type="color"]');
      const text = row.querySelector('input[type="text"]');
      row.dataset.key = definition.key;
      row.dataset.rgb = parsed.rgb;
      const update = (value) => {
        try {
          const snapped = snapHexToGba(value);
          picker.value = snapped.hex;
          text.value = snapped.hex;
          row.dataset.rgb = snapped.rgb;
          text.setCustomValidity("");
        } catch (error) {
          text.setCustomValidity(error.message);
        }
      };
      picker.addEventListener("input", () => update(picker.value));
      text.addEventListener("change", () => update(text.value));
      grid.append(row);
    }
  }

  async saveColours() {
    try {
      const folder = document.querySelector("#project-colour-theme").value;
      let next = this.colourScript;
      document.querySelectorAll("#project-colour-grid .colour-control").forEach((row) => {
        next = replaceThemeField(next, folder, row.dataset.key, row.dataset.rgb);
      });
      await writeText(this.source, COLOUR_SCRIPT, next);
      this.colourScript = next;
      this.colourThemes = parseThemeBlocks(next);
      const status = document.querySelector("#colour-save-status");
      status.textContent = "Colours saved.";
      window.setTimeout(() => { status.textContent = ""; }, 2800);
      this.toast("Project colours saved.", "success");
    } catch (error) {
      this.toast(error.message, "error");
    }
  }

  async loadAssets() {
    this.assets = await listFilesRecursive(this.source, {
      extensions: [".bmp", ".png"],
      maxDepth: 5,
      maxFiles: 900,
      includeHidden: false,
    });
    this.assets = this.assets.filter((item) => item.path.toLocaleLowerCase().startsWith("images/"));
    this.renderAssetList();
  }

  renderAssetList() {
    const query = String(document.querySelector("#asset-search").value || "").toLocaleLowerCase().trim();
    const list = document.querySelector("#project-asset-list");
    list.replaceChildren();
    const fragment = document.createDocumentFragment();
    for (const asset of this.assets) {
      if (query && !asset.path.toLocaleLowerCase().includes(query)) continue;
      const button = document.createElement("button");
      button.type = "button";
      button.className = "asset-item";
      if (this.selectedAsset?.path === asset.path) button.classList.add("is-selected");
      const parts = asset.path.split("/");
      button.innerHTML = `<strong>${escapeHtml(parts.at(-1))}</strong><small>${escapeHtml(parts.slice(0, -1).join(" / "))}</small>`;
      button.addEventListener("click", () => this.selectAsset(asset));
      fragment.append(button);
    }
    list.append(fragment);
  }

  async selectAsset(asset) {
    try {
      this.selectedAsset = asset;
      this.renderAssetList();
      const file = await asset.handle.getFile();
      const source = await imageSourceFromBlob(file);
      const dimensions = sourceDimensions(source);
      if (typeof source.close === "function") source.close();
      if (this.previewUrl) URL.revokeObjectURL(this.previewUrl);
      this.previewUrl = URL.createObjectURL(file);
      const preview = document.querySelector("#project-asset-preview");
      preview.src = this.previewUrl;
      preview.hidden = false;
      document.querySelector("#project-asset-empty").hidden = true;
      document.querySelector("#project-asset-name").textContent = asset.path;
      document.querySelector("#project-asset-size").textContent = `${dimensions.width} x ${dimensions.height}`;
      document.querySelector("#replace-project-asset").disabled = false;
      document.querySelector("#undo-project-asset").disabled = !this.assetSnapshots.has(asset.path);
      asset.dimensions = dimensions;
    } catch (error) {
      this.toast(error.message, "error");
    }
  }

  async replaceSelectedAsset(file) {
    if (!this.selectedAsset) return;
    try {
      const target = await this.selectedAsset.handle.getFile();
      if (!this.assetSnapshots.has(this.selectedAsset.path)) this.assetSnapshots.set(this.selectedAsset.path, target.slice());
      let dimensions = this.selectedAsset.dimensions;
      if (!dimensions) {
        const source = await imageSourceFromBlob(target);
        dimensions = sourceDimensions(source);
        if (typeof source.close === "function") source.close();
      }

      let output;
      if (this.selectedAsset.path.toLocaleLowerCase().endsWith(".png")) {
        const source = await imageSourceFromBlob(file);
        const canvas = document.createElement("canvas");
        canvas.width = dimensions.width;
        canvas.height = dimensions.height;
        const context = canvas.getContext("2d", { alpha: true });
        context.imageSmoothingEnabled = true;
        context.imageSmoothingQuality = "high";
        context.drawImage(source, 0, 0, canvas.width, canvas.height);
        if (typeof source.close === "function") source.close();
        output = await canvasToPng(canvas);
      } else {
        output = await resizeImageToBmp(file, dimensions.width, dimensions.height);
      }
      const writable = await this.selectedAsset.handle.createWritable();
      await writable.write(output);
      await writable.close();
      await this.selectAsset(this.selectedAsset);
      document.querySelector("#undo-project-asset").disabled = false;
      this.toast("Project image replaced.", "success");
    } catch (error) {
      this.toast(error.message, "error");
    }
  }

  async undoSelectedAsset() {
    if (!this.selectedAsset) return;
    const snapshot = this.assetSnapshots.get(this.selectedAsset.path);
    if (!snapshot) return;
    try {
      const writable = await this.selectedAsset.handle.createWritable();
      await writable.write(snapshot);
      await writable.close();
      this.assetSnapshots.delete(this.selectedAsset.path);
      await this.selectAsset(this.selectedAsset);
      document.querySelector("#undo-project-asset").disabled = true;
      this.toast("Project image restored to the version from this session.", "success");
    } catch (error) {
      this.toast(error.message, "error");
    }
  }

  async renderSounds() {
    const list = document.querySelector("#project-sound-list");
    list.replaceChildren();
    for (const definition of SOUND_DEFINITIONS) {
      const path = `source/${definition.file}`;
      const text = await readOptionalText(this.source, path);
      const bytes = parseSoundHeader(text);
      const row = document.createElement("div");
      row.className = "sound-row";
      row.innerHTML = `
        <button class="icon-button compact play-sound" type="button" title="Play ${definition.label}" ${bytes?.length ? "" : "disabled"}><i data-lucide="play"></i><span class="sr-only">Play ${definition.label}</span></button>
        <strong>${definition.label}</strong>
        <span>${definition.description}</span>
        <small>${bytes ? `${(bytes.length / SAMPLE_RATE).toFixed(2)}s` : "Missing"}</small>
        <div class="sound-actions"><button class="button secondary replace-sound" type="button"><i data-lucide="replace"></i><span>Replace</span></button></div>
      `;
      row.querySelector(".play-sound").addEventListener("click", () => {
        if (bytes) playRawAudio(bytes).catch((error) => this.toast(error.message, "error"));
      });
      row.querySelector(".replace-sound").addEventListener("click", () => {
        this.pendingSound = definition;
        document.querySelector("#project-sound-file").click();
      });
      list.append(row);
    }
    window.lucide?.createIcons();
  }

  async replaceSound(definition, file) {
    try {
      const bytes = await decodeAudioToRaw(file, definition.limit);
      await writeText(this.source, `source/${definition.file}`, writeSoundHeader(definition.symbol, bytes));
      await this.renderSounds();
      this.toast(`${definition.label} sound replaced.`, "success");
    } catch (error) {
      this.toast(error.message, "error");
    } finally {
      this.pendingSound = null;
    }
  }

  currentSummary() {
    if (!this.source) return null;
    return {
      name: this.config.name || this.project.name || this.source.name,
      model: this.model,
      version: this.version,
      source: this.source,
    };
  }

  async collectBuildPayload(onProgress = () => {}) {
    if (!this.source || !this.model) throw new Error("Open a Customiser project first.");
    const matchers = [
      (path) => path.toLocaleLowerCase().startsWith("images/") && /\.(bmp|png)$/i.test(path),
      (path) => /^source\/(accept|back|menu|move|tab|startup)_raw\.h$/i.test(path),
      (path) => /^source\/launcher_(customiser_config|text_custom|runtime_text)\.h$/i.test(path),
      (path) => /^source\/launcher_text_languages\.json$/i.test(path),
      (path) => /^grit\/build skin files\.ps1$/i.test(path),
    ];
    onProgress(15, "Collecting project assets...");
    const files = await collectProjectFiles(this.source, matchers, { maxFiles: 900, maxBytes: 36 * 1024 * 1024 });
    onProgress(65, `${files.length} editable files prepared.`);
    return {
      project: safeName(this.config.name || this.project.name || this.source.name),
      model: this.model.key,
      templateVersion: this.version,
      files,
    };
  }
}

function escapeHtml(value) {
  const element = document.createElement("span");
  element.textContent = value;
  return element.innerHTML;
}
