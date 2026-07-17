import {
  chooseFiles,
  copyBrowserFile,
  countFiles,
  ensurePermission,
  findEntryCaseInsensitive,
  getDirectory,
  listDirectory,
  pathExists,
  removeEntry,
  supportsFileSystemAccess,
} from "./filesystem.js";
import { downloadBlob } from "./images.js";
import { ArtworkController } from "./artwork.js";
import { ProjectController } from "./projects.js";

const state = {
  sdRoot: null,
  sdModel: null,
  sdSummary: null,
};

const MODEL_INFO = {
  omega_de: { label: "Omega Definitive Edition", kernel: "ezkernelnew.bin" },
  original: { label: "Original Omega", kernel: "ezkernel.bin" },
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

function requestConfirmation(title, copy, actionLabel = "Continue") {
  const dialog = $("#confirm-dialog");
  $("#confirm-title").textContent = title;
  $("#confirm-copy").textContent = copy;
  $("#confirm-action").textContent = actionLabel;
  return new Promise((resolve) => {
    const onClose = () => {
      dialog.removeEventListener("close", onClose);
      resolve(dialog.returnValue === "confirm");
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
  const deKernel = await findRootFile(root, MODEL_INFO.omega_de.kernel);
  const originalKernel = await findRootFile(root, MODEL_INFO.original.kernel);
  if (deKernel && !originalKernel) return { key: "omega_de", kernelHandle: deKernel };
  if (originalKernel && !deKernel) return { key: "original", kernelHandle: originalKernel };
  if (deKernel && originalKernel) return { key: "ambiguous", kernelHandle: null };
  return { key: "unknown", kernelHandle: null };
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

async function scanSd() {
  if (!state.sdRoot) return;
  const root = state.sdRoot;
  const model = await detectSdModel(root);
  state.sdModel = model.key;

  const [wideCount, squareCount, styleCount] = await Promise.all([
    countFiles(root, "SYSTEM/IMGS", { extensions: [".bmp"], maxDepth: 4, maxFiles: 50000, includeHidden: false }),
    countFiles(root, "SYSTEM/IMGS2", { extensions: [".bmp"], maxDepth: 4, maxFiles: 50000, includeHidden: false }),
    countStyles(root),
  ]);

  let kernelText = "Not found";
  if (model.kernelHandle) {
    const file = await model.kernelHandle.getFile();
    kernelText = `${file.name} (${formatBytes(file.size)})`;
  } else if (model.key === "ambiguous") {
    kernelText = "Two kernel files found";
  }

  state.sdSummary = { model, wideCount, squareCount, styleCount, kernelText };
  renderSdSummary();
  await refreshStyles();
  artwork.onSdChanged();
}

function renderSdSummary() {
  const summary = state.sdSummary;
  if (!summary) return;
  const info = MODEL_INFO[summary.model.key];
  const modelLabel = info?.label || (summary.model.key === "ambiguous" ? "Check card root" : "Not detected");
  $("#sd-model").textContent = modelLabel;
  $("#sd-kernel").textContent = summary.kernelText;
  $("#sd-wide-count").textContent = summary.wideCount.toLocaleString();
  $("#sd-square-count").textContent = summary.squareCount.toLocaleString();
  $("#sd-style-count").textContent = summary.styleCount.toLocaleString();
  $("#sd-connection-title").textContent = state.sdRoot.name;
  $("#sd-connection-copy").textContent = info
    ? "Ready for artwork, styles and kernel maintenance."
    : "The card is connected, but its cartridge model could not be identified from the root kernel file.";
  $("#sd-model-pill").textContent = modelLabel;
  $("#sd-connection-banner").dataset.state = info ? "connected" : "error";
  for (const selector of ["#prepare-folders", "#install-kernel", "#refresh-sd", "#add-style"]) {
    $(selector).disabled = false;
  }
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
    $("#sd-connection-title").textContent = "Reading card...";
    $("#sd-connection-copy").textContent = "Checking artwork and kernel folders.";
    await scanSd();
    toast("SD card connected.", "success");
  } catch (error) {
    if (error.name !== "AbortError") toast(error.message, "error");
  }
}

async function prepareFolders() {
  if (!state.sdRoot) return;
  try {
    for (const path of ["SYSTEM", "SYSTEM/IMGS", "SYSTEM/IMGS/CUSTOM", "SYSTEM/IMGS2", "SYSTEM/IMGS2/CUSTOM", "SYSTEM/KERNELS"]) {
      await getDirectory(state.sdRoot, path, true);
    }
    toast("DS Style folders are ready.", "success");
    await scanSd();
  } catch (error) {
    toast(error.message, "error");
  }
}

async function installKernelFile(file) {
  if (!state.sdRoot || !file) return;
  let model = state.sdModel;
  const lowerName = file.name.toLocaleLowerCase();
  if (lowerName === MODEL_INFO.omega_de.kernel) model = "omega_de";
  if (lowerName === MODEL_INFO.original.kernel) model = "original";
  if (!MODEL_INFO[model]) {
    toast("Name the file ezkernel.bin or ezkernelnew.bin, or connect a card with an existing kernel first.", "error", 6500);
    return;
  }
  const target = MODEL_INFO[model].kernel;
  const exists = await pathExists(state.sdRoot, target, "file");
  if (exists) {
    const confirmed = await requestConfirmation("Replace the installed kernel?", `${target} already exists at the root of this card. It will be replaced with ${file.name}.`, "Replace");
    if (!confirmed) return;
  }
  try {
    await copyBrowserFile(state.sdRoot, target, file);
    toast(`${target} installed. Hold R while the cartridge boots to apply it.`, "success", 6500);
    await scanSd();
  } catch (error) {
    toast(error.message, "error");
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
      const confirmed = await requestConfirmation("Remove this style?", `${entry.name} will be deleted from SYSTEM/KERNELS.`, "Remove");
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
      const confirmed = await requestConfirmation("Replace an existing style?", `${safeFilename} already exists in SYSTEM/KERNELS.`, "Replace");
      if (!confirmed) continue;
    }
    await copyBrowserFile(state.sdRoot, `SYSTEM/KERNELS/${safeFilename}`, file);
    written += 1;
  }
  if (written) toast(`${written} style${written === 1 ? "" : "s"} added.`, "success");
  await scanSd();
}

function updateBuildSummary() {
  const summary = project.currentSummary();
  $("#build-project-name").textContent = summary?.name || "No project open";
  $("#build-project-model").textContent = summary
    ? `${summary.model.label} - DS Style v${summary.version}`
    : "Open a Customiser project first.";
  $("#start-web-build").disabled = !summary;
}

function normalizeServiceUrl(value) {
  return String(value || "").trim().replace(/\/+$/, "");
}

async function checkBuildService(showSuccess = true) {
  const url = normalizeServiceUrl($("#build-service-url").value);
  if (!url) throw new Error("Enter a build service address.");
  localStorage.setItem("ds-style-build-service", url);
  const response = await fetch(`${url}/health`, { headers: { Accept: "application/json" } });
  if (!response.ok) throw new Error(`The build service returned ${response.status}.`);
  const result = await response.json();
  if (!result.ready) throw new Error(result.message || "The build service is not ready.");
  if (showSuccess) toast("Build service is ready.", "success");
  return { url, result };
}

function showBuildProgress(percent, title, copy) {
  const wrapper = $("#build-progress");
  wrapper.hidden = false;
  $("#build-progress-bar").style.width = `${Math.max(0, Math.min(100, percent))}%`;
  if (title) $("#build-progress-title").textContent = title;
  if (copy) $("#build-progress-copy").textContent = copy;
}

async function startBuild() {
  const summary = project.currentSummary();
  if (!summary) return;
  const resultBox = $("#build-result");
  resultBox.hidden = true;
  $("#start-web-build").disabled = true;
  try {
    showBuildProgress(5, "Checking service", "Confirming that the compiler is available...");
    const { url } = await checkBuildService(false);
    const payload = await project.collectBuildPayload((percent, copy) => showBuildProgress(percent, "Preparing project", copy));
    showBuildProgress(72, "Building kernel", "The compiler is processing the project...");
    const response = await fetch(`${url}/api/build`, {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/octet-stream,application/json" },
      body: JSON.stringify(payload),
    });
    if (!response.ok) {
      let message = `Build failed (${response.status}).`;
      try { message = (await response.json()).detail || message; } catch { /* Keep fallback. */ }
      throw new Error(message);
    }
    const blob = await response.blob();
    const filename = response.headers.get("X-DS-Style-Filename") || MODEL_INFO[summary.model.key]?.kernel || "ezkernel.bin";
    showBuildProgress(100, "Build complete", `${filename} is ready.`);
    downloadBlob(blob, filename);
    resultBox.dataset.state = "success";
    resultBox.innerHTML = `<strong>Build complete.</strong><p>${filename} has been downloaded. Put it at the root of your SD card and hold R as the cartridge boots.</p>`;
    resultBox.hidden = false;
    toast("Kernel build complete.", "success");
  } catch (error) {
    showBuildProgress(100, "Build stopped", error.message);
    resultBox.dataset.state = "error";
    resultBox.innerHTML = `<strong>Build failed.</strong><p></p>`;
    resultBox.querySelector("p").textContent = error.message;
    resultBox.hidden = false;
    toast(error.message, "error", 6500);
  } finally {
    $("#start-web-build").disabled = !project.currentSummary();
  }
}

document.querySelectorAll("[data-view-target]").forEach((button) => {
  button.addEventListener("click", () => showView(button.dataset.viewTarget));
});
for (const selector of ["#connect-sd-header", "#connect-sd-main"]) $(selector).addEventListener("click", connectSd);
$("#prepare-folders").addEventListener("click", prepareFolders);
$("#refresh-sd").addEventListener("click", () => scanSd().catch((error) => toast(error.message, "error")));
$("#install-kernel").addEventListener("click", () => $("#kernel-file-input").click());
$("#kernel-file-input").addEventListener("change", (event) => {
  const file = event.target.files?.[0];
  if (file) installKernelFile(file);
  event.target.value = "";
});
$("#add-style").addEventListener("click", () => $("#style-file-input").click());
$("#style-file-input").addEventListener("change", (event) => {
  addStyles([...event.target.files]).catch((error) => toast(error.message, "error"));
  event.target.value = "";
});
$("#check-build-service").addEventListener("click", () => checkBuildService().catch((error) => toast(error.message, "error")));
$("#start-web-build").addEventListener("click", startBuild);

const artwork = new ArtworkController({
  getSdRoot: () => state.sdRoot,
  toast,
  onSaved: scanSd,
});
const project = new ProjectController({ toast, onChanged: updateBuildSummary });

$("#build-service-url").value = localStorage.getItem("ds-style-build-service") || "";
updateBuildSummary();
artwork.onSdChanged();
window.lucide?.createIcons();

if (!supportsFileSystemAccess()) {
  toast("Direct SD-card and project access needs desktop Chrome or Edge. Artwork downloads still work.", "info", 9000);
}

if ("serviceWorker" in navigator && location.protocol !== "file:") {
  navigator.serviceWorker.register("sw.js").catch(() => {});
}
