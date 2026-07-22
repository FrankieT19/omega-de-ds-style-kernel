import {
  copyBrowserFile,
  countFiles,
  ensurePermission,
  findEntryCaseInsensitive,
  getDirectory,
  listDirectory,
  removeEntry,
  supportsFileSystemAccess,
} from "./filesystem.js";
import { ArtworkController } from "./artwork.js";
import {
  installBundledStyle,
  installDsStyle,
  loadInstallManifest,
  loadPersonalisation,
  savePersonalisation,
  updateDsStyle,
} from "./installer.js";
import { LauncherPreview } from "./preview.js";

const state = {
  sdRoot: null,
  sdModel: null,
  sdDetectedModel: null,
  manualSdModel: null,
  sdSummary: null,
  installManifest: null,
  installBusy: false,
  setupRoot: null,
  setupDirty: false,
  personalisation: {
    name: "",
    theme: "Light",
    colour: "Pale Blue",
    language: "English (UK)",
    startScreen: "On",
    startSource: "Last played",
    boot: "Start",
    viewMode: "Horizontal",
    listArt: "Bottom",
    thumbnails: "Title",
    artBorder: "Off",
    roundedCorners: "Off",
    verticalSide: "Center",
    horizontalSide: "Center",
    hideSystemFiles: "On",
    listFolders: "Off",
    cleanList: "Off",
    clock: "24 hour",
    sounds: "On",
    quickStart: "Start",
    launchMode: "Clean",
  },
};

let personalisationRevision = 0;
let personalisationSaveTimer = null;
let personalisationSaveQueue = Promise.resolve();

const MODEL_INFO = {
  omega_de: { label: "Omega Definitive Edition", kernel: "ezkernelnew.bin" },
  original: { label: "Original Omega", kernel: "ezkernel.bin" },
};

const COLOUR_SWATCHES = {
  "Pale Blue": "#52738c",
  "Light Blue": "#299cce",
  "Blue": "#005af7",
  "Dark Blue": "#000094",
  "Green": "#00a439",
  "Pale Green": "#4ac57b",
  "Bright Green": "#00c500",
  "Lime": "#94d600",
  "Yellow": "#d6c500",
  "Red": "#ff0010",
  "Orange": "#ff9400",
  "Brown": "#bd4a00",
  "Pink": "#ff19a4",
  "Pale Pink": "#d673d6",
  "Magenta": "#d600ef",
  "Purple": "#8c00d6",
};

function $(selector) {
  return document.querySelector(selector);
}

function toast(message, type = "info", duration = 4200) {
  const region = $("#toast-region");
  const item = document.createElement("div");
  item.className = "toast";
  item.dataset.type = type;
  const icon = type === "success" ? "circle-check" : type === "error" ? "circle-alert" : "info";
  item.innerHTML = `<i data-lucide="${icon}"></i><span></span>`;
  item.querySelector("span").textContent = message;
  region.append(item);
  window.lucide?.createIcons();
  window.setTimeout(() => item.remove(), duration);
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes)) return "-";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function setSetupState(copy) {
  $("#setup-state").textContent = copy;
}

function setSegmentedValue(control, value) {
  document.querySelectorAll(`[data-setup-control="${control}"] [data-value]`).forEach((button) => {
    const active = button.dataset.value === value;
    button.classList.toggle("is-active", active);
    button.setAttribute("aria-pressed", String(active));
  });
}

function renderPersonalisation() {
  const preferences = state.personalisation;
  $("#setup-name").value = preferences.name;
  $("#setup-colour").value = preferences.colour;
  $("#setup-language").value = preferences.language;
  $("#setup-start-screen").checked = preferences.startScreen === "On";
  $("#setup-start-source").value = preferences.startSource;
  $("#setup-boot").value = preferences.boot;
  $("#setup-view-mode").value = preferences.viewMode;
  $("#setup-list-art").value = preferences.listArt;
  $("#setup-thumbnails").value = preferences.thumbnails;
  $("#setup-art-border").value = preferences.artBorder;
  $("#setup-rounded").value = preferences.roundedCorners;
  $("#setup-vertical-side").value = preferences.verticalSide;
  $("#setup-horizontal-side").value = preferences.horizontalSide;
  $("#setup-hide-system").checked = preferences.hideSystemFiles === "On";
  $("#setup-list-folders").checked = preferences.listFolders === "On";
  $("#setup-clean-list").checked = preferences.cleanList === "On";
  $("#setup-sounds").checked = preferences.sounds === "On";
  $("#setup-quick-start").value = preferences.quickStart;
  $("#setup-launch-mode").value = preferences.launchMode;
  $("#setup-colour-swatch").style.background = COLOUR_SWATCHES[preferences.colour] || COLOUR_SWATCHES["Pale Blue"];
  setSegmentedValue("theme", preferences.theme);
  setSegmentedValue("clock", preferences.clock);
  refreshPersonalisationUi();
}

function markPersonalisationDirty(preference = null) {
  state.setupDirty = true;
  personalisationRevision += 1;
  if (state.sdRoot && state.sdSummary?.hasDsStyle) {
    setSetupState("Saving automatically...");
    schedulePersonalisationSave(personalisationRevision);
  } else {
    setSetupState("Applied during install");
  }
  refreshPersonalisationUi(preference);
}

function refreshPersonalisationUi(preference = null) {
  const preferences = state.personalisation;
  $("#setup-start-source").disabled = preferences.startScreen === "Off";
  $("#setup-list-art-field").hidden = preferences.viewMode !== "List + art";
  if (preference) launcherPreview?.showPreference(preference);
  else launcherPreview?.render();
}

async function loadCardPersonalisation() {
  if (!state.sdRoot || state.setupRoot === state.sdRoot || state.setupDirty) return;
  state.personalisation = await loadPersonalisation(state.sdRoot);
  state.setupRoot = state.sdRoot;
  state.setupDirty = false;
  renderPersonalisation();
  setSetupState(state.sdSummary?.hasDsStyle ? "Changes save automatically" : "Applied during install");
}

function collectPersonalisation() {
  state.personalisation.name = [...$("#setup-name").value.replace(/[\0\r\n]/g, "").trim()].slice(0, 11).join("");
  return { ...state.personalisation };
}

function schedulePersonalisationSave(revision) {
  window.clearTimeout(personalisationSaveTimer);
  const root = state.sdRoot;
  personalisationSaveTimer = window.setTimeout(() => {
    const preferences = collectPersonalisation();
    personalisationSaveQueue = personalisationSaveQueue.then(
      () => saveCurrentSettingsAutomatically(root, revision, preferences),
      () => saveCurrentSettingsAutomatically(root, revision, preferences),
    );
  }, 300);
}

async function saveCurrentSettingsAutomatically(root, revision, preferences) {
  if (!root || root !== state.sdRoot || !state.sdSummary?.hasDsStyle) return;
  try {
    await savePersonalisation(root, preferences);
    if (root === state.sdRoot && revision === personalisationRevision) {
      state.setupDirty = false;
      setSetupState("Saved automatically");
    }
  } catch (error) {
    if (root === state.sdRoot && revision === personalisationRevision) {
      setSetupState("Could not save changes");
      toast(error.message, "error");
    }
  }
}

async function flushAutomaticSettingsSave() {
  window.clearTimeout(personalisationSaveTimer);
  personalisationSaveTimer = null;
  if (state.sdRoot && state.sdSummary?.hasDsStyle && state.setupDirty) {
    const root = state.sdRoot;
    const revision = personalisationRevision;
    const preferences = collectPersonalisation();
    personalisationSaveQueue = personalisationSaveQueue.then(
      () => saveCurrentSettingsAutomatically(root, revision, preferences),
      () => saveCurrentSettingsAutomatically(root, revision, preferences),
    );
  }
  await personalisationSaveQueue;
}

function requestConfirmation(title, copy, actionLabel = "Continue", options = {}) {
  const dialog = $("#confirm-dialog");
  const action = $("#confirm-action");
  $("#confirm-title").textContent = title;
  $("#confirm-copy").textContent = copy;
  action.textContent = actionLabel;
  action.className = `button ${options.danger ? "danger" : "primary"}`;
  dialog.querySelector(".dialog-icon").innerHTML = `<i data-lucide="${options.danger ? "triangle-alert" : "package-check"}"></i>`;
  window.lucide?.createIcons();
  return new Promise((resolve) => {
    const onClose = () => {
      dialog.removeEventListener("close", onClose);
      resolve(dialog.returnValue === "confirm");
    };
    dialog.addEventListener("close", onClose);
    dialog.showModal();
  });
}

function requestCartridgeChoice() {
  const dialog = $("#cartridge-dialog");
  return new Promise((resolve) => {
    const onClose = () => {
      dialog.removeEventListener("close", onClose);
      resolve(MODEL_INFO[dialog.returnValue] ? dialog.returnValue : null);
    };
    dialog.addEventListener("close", onClose);
    dialog.showModal();
  });
}

function showView(viewName) {
  document.querySelectorAll("[data-view]").forEach((view) => {
    const active = view.dataset.view === viewName;
    view.hidden = !active;
    view.classList.toggle("is-active", active);
  });
  document.querySelectorAll("[data-view-target]").forEach((button) => {
    const active = button.dataset.viewTarget === viewName;
    button.classList.toggle("is-active", active);
    if (active) button.setAttribute("aria-current", "page");
    else button.removeAttribute("aria-current");
  });
  if (viewName === "styles" && state.sdRoot) refreshStyles();
  window.scrollTo({ top: 0, behavior: "auto" });
}

async function findRootFile(root, filename) {
  return findEntryCaseInsensitive(root, filename, "file");
}

async function detectSdModel(root) {
  const omegaDe = await findRootFile(root, MODEL_INFO.omega_de.kernel);
  const original = await findRootFile(root, MODEL_INFO.original.kernel);
  let key = "unknown";
  if (omegaDe && !original) key = "omega_de";
  else if (original && !omegaDe) key = "original";
  else if (omegaDe && original) key = "ambiguous";
  return { key, handles: { omega_de: omegaDe, original } };
}

async function countStyles(root) {
  try {
    const directory = await getDirectory(root, "SYSTEM/KERNELS", false);
    const entries = await listDirectory(directory, { filesOnly: true, extension: ".bin" });
    return entries.length;
  } catch {
    return 0;
  }
}

async function hasExistingDsStyle(root) {
  try {
    const system = await getDirectory(root, "SYSTEM", false);
    return Boolean(await findEntryCaseInsensitive(system, "SETTINGS.TXT", "file"));
  } catch {
    return false;
  }
}

async function scanSd() {
  if (!state.sdRoot) return;
  const root = state.sdRoot;
  const detection = await detectSdModel(root);
  state.sdDetectedModel = detection.key;
  if (MODEL_INFO[detection.key]) {
    state.sdModel = detection.key;
    state.manualSdModel = null;
  } else {
    state.sdModel = MODEL_INFO[state.manualSdModel] ? state.manualSdModel : null;
  }

  const [wideCount, squareCount, styleCount, hasDsStyle] = await Promise.all([
    countFiles(root, "SYSTEM/IMGS", { extensions: [".bmp"], maxDepth: 4, maxFiles: 50000, includeHidden: false }),
    countFiles(root, "SYSTEM/IMGS2", { extensions: [".bmp"], maxDepth: 4, maxFiles: 50000, includeHidden: false }),
    countStyles(root),
    hasExistingDsStyle(root),
  ]);

  let kernelText = "Not found";
  if (MODEL_INFO[detection.key]) {
    const file = await detection.handles[detection.key].getFile();
    kernelText = `${file.name} (${formatBytes(file.size)})`;
  } else if (detection.key === "ambiguous") {
    kernelText = "Two kernel files found";
  }

  state.sdSummary = { detection, wideCount, squareCount, styleCount, kernelText, hasDsStyle };
  await loadCardPersonalisation();
  renderSdSummary();
  await refreshStyles();
  artwork.onSdChanged();
}

function renderSdSummary() {
  const summary = state.sdSummary;
  if (!summary) return;
  const info = MODEL_INFO[state.sdModel];
  const modelLabel = info?.label || "Choose cartridge";
  const canChoose = Boolean(state.sdRoot) && !MODEL_INFO[summary.detection.key];
  const cartridgeButton = $("#choose-cartridge");

  $("#sd-model").textContent = modelLabel;
  $("#sd-kernel").textContent = summary.kernelText;
  $("#sd-wide-count").textContent = summary.wideCount.toLocaleString();
  $("#sd-square-count").textContent = summary.squareCount.toLocaleString();
  $("#sd-style-count").textContent = summary.styleCount.toLocaleString();
  $("#sd-connection-title").textContent = state.sdRoot.name;
  $("#sd-connection-copy").textContent = info
    ? summary.hasDsStyle
      ? "Existing DS Style install found. Ready to update the kernel or manage artwork and styles."
      : "Ready to install DS Style and manage artwork or styles."
    : "Choose the cartridge that will use this SD card to continue.";
  $("#sd-model-pill").textContent = info ? modelLabel : "Choose cartridge";
  $("#sd-connection-banner").dataset.state = info ? "connected" : "choice";

  cartridgeButton.disabled = !canChoose;
  cartridgeButton.classList.toggle("is-selectable", canChoose);
  cartridgeButton.title = canChoose ? "Choose cartridge" : "Detected from the kernel file at the SD-card root";
  refreshInstallerActions();
  $("#refresh-sd").disabled = false;
  $("#add-style").disabled = false;
  document.querySelectorAll(".bundled-style").forEach((button) => {
    button.disabled = !info;
  });
}

function refreshInstallerActions() {
  const connected = Boolean(state.sdRoot);
  const modelKnown = Boolean(MODEL_INFO[state.sdModel]);
  const hasDsStyle = Boolean(state.sdSummary?.hasDsStyle);
  $("#quick-install").disabled = state.installBusy || !connected;
  $("#quick-update").disabled = state.installBusy || !connected || !hasDsStyle;

  const availability = $("#update-availability");
  if (!connected) availability.textContent = "Connect an existing DS Style SD card to update only its kernel.";
  else if (!hasDsStyle) availability.textContent = "No existing DS Style install was found. Use Install DS Style for a new setup.";
  else if (!modelKnown) availability.textContent = "Choose your cartridge when you update. Settings and personal files stay unchanged.";
  else availability.textContent = "Updates only the kernel. Settings and personal files stay unchanged.";
}

async function chooseCartridge() {
  if (!state.sdRoot || MODEL_INFO[state.sdDetectedModel]) return state.sdModel;
  const model = await requestCartridgeChoice();
  if (!model) return null;
  state.manualSdModel = model;
  state.sdModel = model;
  renderSdSummary();
  toast(`${MODEL_INFO[model].label} selected.`, "success");
  return model;
}

async function connectSd() {
  if (!supportsFileSystemAccess()) {
    toast("SD-card access needs Chrome or Edge on desktop.", "error", 6500);
    return;
  }
  try {
    const handle = await window.showDirectoryPicker({ id: "ds-style-sd", mode: "readwrite" });
    if (!await ensurePermission(handle, "readwrite")) throw new Error("Write access was not granted.");
    state.sdRoot = handle;
    state.sdModel = null;
    state.sdDetectedModel = null;
    state.manualSdModel = null;
    state.sdSummary = null;
    state.setupRoot = null;
    state.setupDirty = false;
    personalisationRevision += 1;
    window.clearTimeout(personalisationSaveTimer);
    $("#quick-install").disabled = true;
    $("#quick-update").disabled = true;
    $("#sd-connection-title").textContent = "Reading card...";
    $("#sd-connection-copy").textContent = "Checking the kernel, artwork and style folders.";
    await scanSd();
    toast("SD card connected.", "success");
  } catch (error) {
    if (error.name !== "AbortError") toast(error.message, "error");
  }
}

function showInstallProgress(detail) {
  const wrapper = $("#install-progress");
  wrapper.hidden = false;
  $("#install-progress-bar").style.width = `${Math.max(0, Math.min(100, detail.percent || 0))}%`;
  if (detail.title) $("#install-progress-title").textContent = detail.title;
  if (detail.copy) $("#install-progress-copy").textContent = detail.copy;
}

function showInstallResult(stateName, title, copy) {
  const result = $("#install-result");
  result.dataset.state = stateName;
  result.querySelector("svg, i")?.remove();
  result.insertAdjacentHTML("afterbegin", `<i data-lucide="${stateName === "success" ? "circle-check" : "circle-alert"}"></i>`);
  $("#install-result-title").textContent = title;
  $("#install-result-copy").textContent = copy;
  result.hidden = false;
  window.lucide?.createIcons();
}

async function startQuickInstall() {
  if (!state.sdRoot) return;
  let model = state.sdModel;
  if (!MODEL_INFO[model]) model = await chooseCartridge();
  if (!MODEL_INFO[model]) return;

  let manifest = state.installManifest;
  try {
    manifest ||= await loadInstallManifest();
    state.installManifest = manifest;
  } catch (error) {
    toast(error.message, "error", 6500);
    return;
  }

  const info = MODEL_INFO[model];
  const confirmed = await requestConfirmation(
    `Install DS Style v${manifest.version}?`,
    `The card will be prepared for the ${info.label}. Recognised stock or Simple folders will be moved into SYSTEM, and existing personal files will be preserved.`,
    "Install",
  );
  if (!confirmed) return;

  await flushAutomaticSettingsSave();

  state.installBusy = true;
  refreshInstallerActions();
  $("#install-result").hidden = true;
  try {
    const result = await installDsStyle(state.sdRoot, model, collectPersonalisation(), showInstallProgress);
    const backupNote = result.backedUp
      ? ` ${result.backedUp} existing file${result.backedUp === 1 ? " was" : "s were"} preserved in SYSTEM/BACKUP/WEB INSTALL BACKUP.`
      : "";
    showInstallResult(
      "success",
      `DS Style v${result.version} is ready`,
      `Safely eject the SD card, insert it into the cartridge, then hold R while the cartridge boots. Keep the console powered on until the update finishes.${backupNote}`,
    );
    toast("DS Style was installed on the SD card.", "success", 6500);
    state.setupDirty = false;
    setSetupState("Saved automatically");
    await scanSd();
  } catch (error) {
    showInstallResult("error", "Installation stopped", `${error.message} It is safe to reconnect the card and try again.`);
    toast(error.message, "error", 7000);
  } finally {
    state.installBusy = false;
    refreshInstallerActions();
  }
}

async function startQuickUpdate() {
  if (!state.sdRoot || !state.sdSummary?.hasDsStyle) return;
  let model = state.sdModel;
  if (!MODEL_INFO[model]) model = await chooseCartridge();
  if (!MODEL_INFO[model]) return;

  let manifest = state.installManifest;
  try {
    manifest ||= await loadInstallManifest();
    state.installManifest = manifest;
  } catch (error) {
    toast(error.message, "error", 6500);
    return;
  }

  const info = MODEL_INFO[model];
  const confirmed = await requestConfirmation(
    `Update DS Style to v${manifest.version}?`,
    `Only the kernel update file for the ${info.label} will be replaced. Your settings, saves, artwork, cheats and installed styles will not be changed.`,
    "Update",
  );
  if (!confirmed) return;

  await flushAutomaticSettingsSave();

  state.installBusy = true;
  refreshInstallerActions();
  $("#install-result").hidden = true;
  try {
    const result = await updateDsStyle(state.sdRoot, model, showInstallProgress);
    showInstallResult(
      "success",
      `DS Style v${result.version} is ready`,
      "Safely eject the SD card, insert it into the cartridge, then hold R while the cartridge boots. Keep the console powered on until the update finishes.",
    );
    toast("DS Style was updated on the SD card.", "success", 6500);
    await scanSd();
  } catch (error) {
    showInstallResult("error", "Update stopped", `${error.message} It is safe to reconnect the card and try again.`);
    toast(error.message, "error", 7000);
  } finally {
    state.installBusy = false;
    refreshInstallerActions();
  }
}

async function refreshStyles() {
  const empty = $("#styles-empty");
  const list = $("#style-list");
  list.replaceChildren();
  if (!state.sdRoot) {
    empty.hidden = false;
    list.hidden = true;
    return;
  }

  let entries = [];
  try {
    const directory = await getDirectory(state.sdRoot, "SYSTEM/KERNELS", true);
    entries = await listDirectory(directory, { filesOnly: true, extension: ".bin" });
  } catch (error) {
    toast(error.message, "error");
  }
  if (!entries.length) {
    empty.querySelector("h2").textContent = "No styles installed";
    empty.querySelector("p").textContent = "Add a .bin kernel to make it available through Load style.";
    empty.hidden = false;
    list.hidden = true;
    return;
  }

  empty.hidden = true;
  list.hidden = false;
  const fragment = document.createDocumentFragment();
  for (const entry of entries) {
    const file = await entry.handle.getFile();
    const row = document.createElement("div");
    row.className = "style-row";
    row.innerHTML = `
      <div class="action-symbol"><i data-lucide="layers-3"></i></div>
      <div><strong></strong><small></small></div>
      <button class="icon-button remove-style" type="button" title="Remove style"><i data-lucide="trash-2"></i><span class="sr-only">Remove style</span></button>
    `;
    row.querySelector("strong").textContent = entry.name.replace(/\.bin$/i, "");
    row.querySelector("small").textContent = `${entry.name} - ${formatBytes(file.size)}`;
    row.querySelector(".remove-style").addEventListener("click", async () => {
      const confirmed = await requestConfirmation(
        "Remove this style?",
        `${entry.name} will be deleted from SYSTEM/KERNELS.`,
        "Remove",
        { danger: true },
      );
      if (!confirmed) return;
      try {
        await removeEntry(state.sdRoot, `SYSTEM/KERNELS/${entry.name}`);
        toast("Style removed.", "success");
        await scanSd();
      } catch (error) {
        toast(error.message, "error");
      }
    });
    fragment.append(row);
  }
  list.append(fragment);
  window.lucide?.createIcons();
}

async function addStyles(files) {
  if (!state.sdRoot || !files?.length) return;
  const directory = await getDirectory(state.sdRoot, "SYSTEM/KERNELS", true);
  let written = 0;
  for (const file of files) {
    if (!file.name.toLocaleLowerCase().endsWith(".bin")) continue;
    const safeFilename = file.name.replace(/[<>:"/\\|?*]/g, "").trim();
    if (!safeFilename) continue;
    const existing = await findEntryCaseInsensitive(directory, safeFilename, "file");
    if (existing) {
      const confirmed = await requestConfirmation(
        "Replace an existing style?",
        `${safeFilename} already exists in SYSTEM/KERNELS.`,
        "Replace",
        { danger: true },
      );
      if (!confirmed) continue;
    }
    await copyBrowserFile(state.sdRoot, `SYSTEM/KERNELS/${safeFilename}`, file);
    written += 1;
  }
  if (written) toast(`${written} style${written === 1 ? "" : "s"} added.`, "success");
  await scanSd();
}

async function addBundledStyle(styleId, button) {
  if (!state.sdRoot) return;
  let model = state.sdModel;
  if (!MODEL_INFO[model]) model = await chooseCartridge();
  if (!MODEL_INFO[model]) return;
  const label = styleId === "standard" ? "DS Style" : styleId === "analogue" ? "Analogue Style" : "Simple Style";
  const confirmed = await requestConfirmation(
    `Add ${label}?`,
    `${label} will be added to SYSTEM/KERNELS for the ${MODEL_INFO[model].label}. If it is already there, the copy on the card will be refreshed.`,
    "Add to card",
  );
  if (!confirmed) return;
  const copy = button.querySelector("span");
  const originalCopy = copy.textContent;
  button.disabled = true;
  copy.textContent = "Adding...";
  try {
    const result = await installBundledStyle(state.sdRoot, model, styleId);
    toast(`${result.label} added to the card.`, "success");
    await scanSd();
  } catch (error) {
    toast(error.message, "error", 7000);
  } finally {
    copy.textContent = originalCopy;
    button.disabled = !state.sdRoot || !MODEL_INFO[state.sdModel];
  }
}

async function loadInstallerInfo() {
  try {
    state.installManifest = await loadInstallManifest();
    $("#installer-version").textContent = `DS Style v${state.installManifest.version}`;
  } catch {
    $("#installer-version").textContent = "Installer temporarily unavailable";
  }
}

document.querySelectorAll("[data-view-target]").forEach((button) => {
  button.addEventListener("click", () => showView(button.dataset.viewTarget));
});
document.querySelectorAll("[data-setup-control] [data-value]").forEach((button) => {
  button.addEventListener("click", () => {
    const control = button.closest("[data-setup-control]").dataset.setupControl;
    state.personalisation[control === "clock" ? "clock" : "theme"] = button.dataset.value;
    setSegmentedValue(control, button.dataset.value);
    markPersonalisationDirty(control === "clock" ? "clock" : "theme");
  });
});
$("#setup-name").addEventListener("input", (event) => {
  state.personalisation.name = [...event.target.value.replace(/[\0\r\n]/g, "")].slice(0, 11).join("");
  if (event.target.value !== state.personalisation.name) event.target.value = state.personalisation.name;
  markPersonalisationDirty("name");
});
const setupSelectBindings = {
  "#setup-colour": "colour",
  "#setup-language": "language",
  "#setup-start-source": "startSource",
  "#setup-boot": "boot",
  "#setup-view-mode": "viewMode",
  "#setup-list-art": "listArt",
  "#setup-thumbnails": "thumbnails",
  "#setup-art-border": "artBorder",
  "#setup-rounded": "roundedCorners",
  "#setup-vertical-side": "verticalSide",
  "#setup-horizontal-side": "horizontalSide",
  "#setup-quick-start": "quickStart",
  "#setup-launch-mode": "launchMode",
};
for (const [selector, preference] of Object.entries(setupSelectBindings)) {
  $(selector).addEventListener("change", (event) => {
    state.personalisation[preference] = event.target.value;
    if (preference === "colour") {
      $("#setup-colour-swatch").style.background = COLOUR_SWATCHES[event.target.value] || COLOUR_SWATCHES["Pale Blue"];
    }
    markPersonalisationDirty(preference);
  });
}
const setupToggleBindings = {
  "#setup-start-screen": "startScreen",
  "#setup-hide-system": "hideSystemFiles",
  "#setup-list-folders": "listFolders",
  "#setup-clean-list": "cleanList",
  "#setup-sounds": "sounds",
};
for (const [selector, preference] of Object.entries(setupToggleBindings)) {
  $(selector).addEventListener("change", (event) => {
    state.personalisation[preference] = event.target.checked ? "On" : "Off";
    markPersonalisationDirty(preference);
  });
}
document.querySelectorAll("[data-preview-scene]").forEach((button) => {
  button.addEventListener("click", () => {
    document.querySelectorAll("[data-preview-scene]").forEach((candidate) => candidate.classList.toggle("is-active", candidate === button));
    launcherPreview.setScene(button.dataset.previewScene);
  });
});
for (const selector of ["#connect-sd-header", "#connect-sd-main"]) $(selector).addEventListener("click", connectSd);
$("#choose-cartridge").addEventListener("click", chooseCartridge);
$("#quick-install").addEventListener("click", startQuickInstall);
$("#quick-update").addEventListener("click", startQuickUpdate);
$("#refresh-sd").addEventListener("click", () => scanSd().catch((error) => toast(error.message, "error")));
$("#add-style").addEventListener("click", () => $("#style-file-input").click());
$("#style-file-input").addEventListener("change", (event) => {
  addStyles([...event.target.files]).catch((error) => toast(error.message, "error"));
  event.target.value = "";
});
document.querySelectorAll(".bundled-style").forEach((button) => {
  button.addEventListener("click", () => addBundledStyle(button.dataset.bundledStyle, button));
});

const launcherPreview = new LauncherPreview(
  $("#launcher-preview"),
  () => state.personalisation,
  (copy) => { $("#launcher-preview-copy").textContent = copy; },
);

const artwork = new ArtworkController({
  getSdRoot: () => state.sdRoot,
  toast,
  onSaved: scanSd,
});

loadInstallerInfo();
renderPersonalisation();
artwork.onSdChanged();
window.lucide?.createIcons();

if (!supportsFileSystemAccess()) {
  toast("Direct SD-card access needs desktop Chrome or Edge. Artwork downloads still work.", "info", 9000);
}

if ("serviceWorker" in navigator && location.protocol !== "file:") {
  navigator.serviceWorker.register("sw.js").catch(() => {});
}
