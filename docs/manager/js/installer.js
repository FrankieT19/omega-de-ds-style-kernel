import {
  findEntryCaseInsensitive,
  getDirectory,
  pathExists,
  readText,
  writeFile,
} from "./filesystem.js";

const PACKAGE_ROOT = new URL("../packages/current/", import.meta.url);
const MANIFEST_URL = new URL("manifest.json", PACKAGE_ROOT);
const BACKUP_ROOT = "SYSTEM/BACKUP/WEB INSTALL BACKUP";
const MIGRATION_FOLDERS = [
  "BACKUP",
  "CHEAT",
  "IMGS",
  "IMGS2",
  "KERNELS",
  "PATCH",
  "PLUG",
  "RTS",
  "SAVER",
];
const KERNEL_FILES = {
  omega_de: "ezkernelnew.bin",
  original: "ezkernel.bin",
};
const NAME_NOTE = "# The name shown on the top bar displays up to 11 characters.";
const PERSONAL_SETTING_OPTIONS = {
  "Theme": ["Light", "Dark"],
  "Colour": ["Pale Blue", "Light Blue", "Blue", "Dark Blue", "Green", "Pale Green", "Bright Green", "Lime", "Yellow", "Red", "Orange", "Brown", "Pink", "Pale Pink", "Magenta", "Purple"],
  "Language": ["English (UK)", "English (US)", "Español", "Français", "Português", "Deutsch", "Türkçe", "Italiano", "Nederlands", "Svenska", "Suomi", "Chinese", "ภาษาไทย"],
  "Start screen": ["On", "Off"],
  "Start screen source": ["Last played", "Favourites"],
  "Boot to": ["Start", "SD", "NOR", "Last game", "Recents", "Favourites"],
  "View mode": ["List", "List + art", "Horizontal", "Vertical"],
  "List art": ["Top", "Center", "Bottom"],
  "Thumbnails": ["Title", "Box"],
  "Art border": ["Off", "Accent", "Black", "Grey", "White"],
  "Rounded corners": ["Full", "No Start", "Off"],
  "Vertical side": ["Center", "Left", "Right"],
  "Horizontal side": ["Center", "Top", "Bottom"],
  "Hide system files": ["On", "Off"],
  "List folders": ["On", "Off"],
  "Clean list": ["On", "Off"],
  "Clock format": ["24 hour", "12 hour"],
  "Sounds": ["On", "Off"],
  "Quick start hotkey": ["Start", "Select", "L", "A", "B"],
  "Last launch mode": ["Clean", "Addon"],
};
const PERSONAL_DEFAULTS = Object.freeze({
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
});
const PREFERENCE_KEYS = {
  theme: "Theme",
  colour: "Colour",
  language: "Language",
  startScreen: "Start screen",
  startSource: "Start screen source",
  boot: "Boot to",
  viewMode: "View mode",
  listArt: "List art",
  thumbnails: "Thumbnails",
  artBorder: "Art border",
  roundedCorners: "Rounded corners",
  verticalSide: "Vertical side",
  horizontalSide: "Horizontal side",
  hideSystemFiles: "Hide system files",
  listFolders: "List folders",
  cleanList: "Clean list",
  clock: "Clock format",
  sounds: "Sounds",
  quickStart: "Quick start hotkey",
  launchMode: "Last launch mode",
};

let manifestPromise = null;

function normaliseLines(text) {
  return String(text || "").replace(/^\uFEFF/, "").replace(/\r\n|\r/g, "\n");
}

function parseSettingLine(line) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith("#") || trimmed.startsWith(";") || !trimmed.includes("=")) return null;
  const separator = trimmed.indexOf("=");
  return {
    key: trimmed.slice(0, separator).trim(),
    value: trimmed.slice(separator + 1).trim(),
  };
}

function findCanonicalSetting(key) {
  const wanted = key.toLocaleLowerCase();
  return Object.keys(PERSONAL_SETTING_OPTIONS).find((candidate) => candidate.toLocaleLowerCase() === wanted) || null;
}

function sanitiseName(value) {
  return [...String(value || "").replace(/[\0\r\n]/g, "").trim()].slice(0, 11).join("");
}

function validatePersonalisation(input = {}) {
  const result = { ...PERSONAL_DEFAULTS, name: sanitiseName(input.name) };
  for (const [preference, setting] of Object.entries(PREFERENCE_KEYS)) {
    const value = String(input[preference] ?? PERSONAL_DEFAULTS[preference]);
    result[preference] = PERSONAL_SETTING_OPTIONS[setting].includes(value) ? value : PERSONAL_DEFAULTS[preference];
  }
  return result;
}

async function readOptionalText(root, path) {
  try {
    return await readText(root, path);
  } catch (error) {
    if (error?.name === "NotFoundError") return "";
    throw error;
  }
}

export async function loadPersonalisation(root) {
  const result = { ...PERSONAL_DEFAULTS };
  const [settingsText, nameText] = await Promise.all([
    readOptionalText(root, "SYSTEM/SETTINGS.TXT"),
    readOptionalText(root, "SYSTEM/NAME.TXT"),
  ]);

  result.name = sanitiseName(normaliseLines(nameText).split("\n", 1)[0] || "");
  const reverseKeys = Object.fromEntries(Object.entries(PREFERENCE_KEYS).map(([preference, setting]) => [setting, preference]));
  for (const line of normaliseLines(settingsText).split("\n")) {
    const parsed = parseSettingLine(line);
    if (!parsed) continue;
    const setting = findCanonicalSetting(parsed.key);
    if (!setting || !PERSONAL_SETTING_OPTIONS[setting].includes(parsed.value)) continue;
    result[reverseKeys[setting]] = parsed.value;
  }
  return result;
}

function updateSettingsText(existingText, preferences) {
  const eol = String(existingText || "").includes("\r\n") ? "\r\n" : "\n";
  const replacements = new Map();
  for (const [preference, setting] of Object.entries(PREFERENCE_KEYS)) {
    replacements.set(setting, preferences[preference]);
  }

  let lines = normaliseLines(existingText).split("\n");
  if (!String(existingText || "").trim()) {
    lines = [
      "# DS Style settings",
      "# Preferences chosen with DS Style Manager. Other settings use the kernel defaults.",
      "",
    ];
  }

  const written = new Set();
  const output = [];
  for (const line of lines) {
    const parsed = parseSettingLine(line);
    const setting = parsed ? findCanonicalSetting(parsed.key) : null;
    if (!setting || !replacements.has(setting)) {
      output.push(line);
      continue;
    }
    if (written.has(setting)) continue;
    output.push(`${setting} = ${replacements.get(setting)}`);
    written.add(setting);
  }

  for (const setting of Object.keys(PERSONAL_SETTING_OPTIONS)) {
    if (!replacements.has(setting) || written.has(setting)) continue;
    if (output.length && output.at(-1) !== "") output.push("");
    output.push(`${setting} = ${replacements.get(setting)}`);
  }
  while (output.length > 1 && output.at(-1) === "") output.pop();
  return `${output.join(eol)}${eol}`;
}

function updateNameText(existingText, name) {
  const eol = String(existingText || "").includes("\r\n") ? "\r\n" : "\n";
  const lines = normaliseLines(existingText).split("\n");
  lines[0] = sanitiseName(name);
  while (lines.length > 1 && lines.at(-1) === "") lines.pop();
  if (!lines.some((line) => line.trim() === NAME_NOTE)) lines.push(NAME_NOTE);
  return `${lines.join(eol)}${eol}`;
}

async function applyPersonalisation(root, input, onProgress) {
  const preferences = validatePersonalisation(input);
  report(onProgress, {
    percent: 99,
    title: "Applying your preferences",
    copy: "Saving the name, appearance and menu choices...",
  });
  const [settingsText, nameText] = await Promise.all([
    readOptionalText(root, "SYSTEM/SETTINGS.TXT"),
    readOptionalText(root, "SYSTEM/NAME.TXT"),
  ]);
  await Promise.all([
    writeFile(root, "SYSTEM/SETTINGS.TXT", updateSettingsText(settingsText, preferences)),
    writeFile(root, "SYSTEM/NAME.TXT", updateNameText(nameText, preferences.name)),
  ]);
  return preferences;
}

export async function savePersonalisation(root, input) {
  return applyPersonalisation(root, input);
}

function report(onProgress, detail) {
  onProgress?.(detail);
}

function toHex(bytes) {
  return [...bytes].map((value) => value.toString(16).padStart(2, "0")).join("");
}

async function verifyBytes(bytes, entry) {
  if (Number.isFinite(entry.size) && bytes.byteLength !== entry.size) {
    throw new Error(`The installer download for ${entry.target} is incomplete.`);
  }
  if (entry.sha256 && globalThis.crypto?.subtle) {
    const digest = await crypto.subtle.digest("SHA-256", bytes);
    if (toHex(new Uint8Array(digest)) !== entry.sha256.toLocaleLowerCase()) {
      throw new Error(`The installer download for ${entry.target} failed its safety check.`);
    }
  }
}

async function fetchPackageEntryOnce(entry, retry = false) {
  const url = new URL(entry.source, PACKAGE_ROOT);
  url.searchParams.set("sha256", entry.sha256 || String(entry.size || "current"));
  if (retry) url.searchParams.set("retry", String(Date.now()));
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) throw new Error(`Could not download ${entry.target} (${response.status}).`);
  const bytes = await response.arrayBuffer();
  await verifyBytes(bytes, entry);
  return { entry, bytes };
}

async function fetchPackageEntry(entry) {
  try {
    return await fetchPackageEntryOnce(entry, false);
  } catch {
    return fetchPackageEntryOnce(entry, true);
  }
}

export function loadInstallManifest(force = false) {
  if (force || !manifestPromise) {
    const url = new URL(MANIFEST_URL);
    if (force) url.searchParams.set("refresh", String(Date.now()));
    manifestPromise = fetch(url, { cache: "no-store" })
      .then((response) => {
        if (!response.ok) throw new Error(`The installer package is unavailable (${response.status}).`);
        return response.json();
      })
      .then((manifest) => {
        if (manifest.schema !== 1 || !manifest.version || !manifest.kernels || !Array.isArray(manifest.files)) {
          throw new Error("The installer package is not recognised.");
        }
        return manifest;
      })
      .catch((error) => {
        manifestPromise = null;
        throw error;
      });
  }
  return manifestPromise;
}

export async function installBundledStyle(root, model, styleId) {
  if (!KERNEL_FILES[model]) throw new Error("Choose a supported cartridge before adding a style.");
  const manifest = await loadInstallManifest();
  let entry;
  let label;
  if (styleId === "standard") {
    label = "DS Style";
    entry = {
      ...manifest.kernels[model],
      target: `SYSTEM/KERNELS/DS Style v${manifest.version}.bin`,
    };
  } else {
    const style = manifest.styles?.[styleId];
    entry = style?.models?.[model];
    label = style?.label;
  }
  if (!entry || !label) throw new Error("That style is not available for the selected cartridge.");
  const payload = await fetchPackageEntry(entry);
  await writeFile(root, entry.target, payload.bytes);
  return { label, target: entry.target, version: manifest.version };
}

export async function updateDsStyle(root, model, onProgress = null) {
  if (!KERNEL_FILES[model]) throw new Error("Choose a supported cartridge before updating DS Style.");

  report(onProgress, {
    percent: 8,
    title: "Preparing update",
    copy: "Checking the latest DS Style kernel...",
  });
  const manifest = await loadInstallManifest();
  const manifestKernel = manifest.kernels[model];
  if (!manifestKernel) throw new Error("The selected cartridge is not supported by this updater.");
  const kernel = { ...manifestKernel, target: KERNEL_FILES[model] };

  report(onProgress, {
    percent: 24,
    title: `Downloading DS Style v${manifest.version}`,
    copy: "Verifying the kernel update file...",
  });
  const payload = await fetchPackageEntry(kernel);

  report(onProgress, {
    percent: 78,
    title: "Updating DS Style",
    copy: `Writing ${kernel.target} to the SD-card root...`,
  });
  await writeFile(root, kernel.target, payload.bytes);

  report(onProgress, {
    percent: 100,
    title: "DS Style is ready",
    copy: "Your existing settings and personal files were left unchanged.",
  });
  return { version: manifest.version, kernel: kernel.target };
}

async function preparePayload(model, onProgress) {
  report(onProgress, { percent: 2, title: "Preparing installer", copy: "Checking the latest DS Style package..." });
  const manifest = await loadInstallManifest();
  const kernel = manifest.kernels[model];
  if (!kernel) throw new Error("The selected cartridge is not supported by this installer.");

  const entries = [...manifest.files, kernel];
  const payload = new Array(entries.length);
  let cursor = 0;
  let complete = 0;
  const workerCount = Math.min(6, entries.length);

  async function worker() {
    while (cursor < entries.length) {
      const index = cursor;
      cursor += 1;
      payload[index] = await fetchPackageEntry(entries[index]);
      complete += 1;
      report(onProgress, {
        percent: 4 + Math.round((complete / entries.length) * 26),
        title: `Downloading DS Style v${manifest.version}`,
        copy: `${complete} of ${entries.length} files ready...`,
      });
    }
  }

  await Promise.all(Array.from({ length: workerCount }, () => worker()));
  return { manifest, payload, kernelTarget: kernel.target };
}

async function writeDirectoryFile(directory, filename, data, existingHandle = null) {
  const handle = existingHandle || await directory.getFileHandle(filename, { create: true });
  const writable = await handle.createWritable();
  try {
    await writable.write(data);
  } finally {
    await writable.close();
  }
  return handle;
}

async function filesMatch(left, right) {
  if (left.size !== right.size) return false;
  const chunkSize = 256 * 1024;
  for (let offset = 0; offset < left.size; offset += chunkSize) {
    const end = Math.min(left.size, offset + chunkSize);
    const [leftBytes, rightBytes] = await Promise.all([
      left.slice(offset, end).arrayBuffer(),
      right.slice(offset, end).arrayBuffer(),
    ]);
    const a = new Uint8Array(leftBytes);
    const b = new Uint8Array(rightBytes);
    for (let index = 0; index < a.length; index += 1) {
      if (a[index] !== b[index]) return false;
    }
  }
  return true;
}

function splitFilename(filename) {
  const dot = filename.lastIndexOf(".");
  if (dot <= 0) return { stem: filename, extension: "" };
  return { stem: filename.slice(0, dot), extension: filename.slice(dot) };
}

async function uniqueFileHandle(directory, filename) {
  const { stem, extension } = splitFilename(filename);
  for (let suffix = 0; suffix < 1000; suffix += 1) {
    const candidate = suffix === 0 ? filename : `${stem} (${suffix + 1})${extension}`;
    if (!await findEntryCaseInsensitive(directory, candidate)) {
      return directory.getFileHandle(candidate, { create: true });
    }
  }
  throw new Error(`Could not create a safe backup name for ${filename}.`);
}

async function preserveExistingFile(root, migrationFolder, relativeParts, filename, file) {
  const parent = [BACKUP_ROOT, migrationFolder, ...relativeParts].join("/");
  const directory = await getDirectory(root, parent, true);
  const handle = await uniqueFileHandle(directory, filename);
  await writeDirectoryFile(directory, filename, file, handle);
}

async function directoryIsEmpty(directory) {
  for await (const _entry of directory.entries()) return false;
  return true;
}

async function migrateDirectory(root, source, target, migrationFolder, relativeParts, stats, onProgress) {
  const entries = [];
  for await (const entry of source.entries()) entries.push(entry);

  for (const [name, sourceHandle] of entries) {
    const targetHandle = await findEntryCaseInsensitive(target, name);
    if (sourceHandle.kind === "directory") {
      if (targetHandle?.kind === "file") {
        throw new Error(`Cannot move ${migrationFolder}/${[...relativeParts, name].join("/")} because a file is in its place.`);
      }
      const targetDirectory = targetHandle || await target.getDirectoryHandle(name, { create: true });
      await migrateDirectory(root, sourceHandle, targetDirectory, migrationFolder, [...relativeParts, name], stats, onProgress);
      if (await directoryIsEmpty(sourceHandle)) await source.removeEntry(name);
      continue;
    }

    if (targetHandle?.kind === "directory") {
      throw new Error(`Cannot move ${migrationFolder}/${[...relativeParts, name].join("/")} because a folder is in its place.`);
    }

    const sourceFile = await sourceHandle.getFile();
    if (targetHandle) {
      const targetFile = await targetHandle.getFile();
      if (!await filesMatch(sourceFile, targetFile)) {
        await preserveExistingFile(root, migrationFolder, relativeParts, name, targetFile);
        stats.backedUp += 1;
      } else {
        stats.duplicates += 1;
      }
    }

    await writeDirectoryFile(target, name, sourceFile, targetHandle);
    await source.removeEntry(name);
    stats.moved += 1;
    if (stats.moved % 20 === 0) {
      report(onProgress, {
        percent: stats.migrationPercent,
        title: "Organising the SD card",
        copy: `Moving ${migrationFolder} files into SYSTEM... (${stats.moved.toLocaleString()} moved)`,
      });
    }
  }
}

async function migrateRootFolders(root, onProgress) {
  const stats = { moved: 0, backedUp: 0, duplicates: 0, migrationPercent: 32 };
  for (let index = 0; index < MIGRATION_FOLDERS.length; index += 1) {
    const folder = MIGRATION_FOLDERS[index];
    const source = await findEntryCaseInsensitive(root, folder, "directory");
    if (!source) continue;
    stats.migrationPercent = 32 + Math.round(((index + 1) / MIGRATION_FOLDERS.length) * 30);
    report(onProgress, {
      percent: stats.migrationPercent,
      title: "Organising the SD card",
      copy: `Moving ${folder} into SYSTEM...`,
    });
    const target = await getDirectory(root, `SYSTEM/${folder}`, true);
    await migrateDirectory(root, source, target, folder, [], stats, onProgress);
    if (await directoryIsEmpty(source)) await root.removeEntry(source.name);
  }
  return stats;
}

async function preserveOtherKernel(root, model, stats, onProgress) {
  const otherModel = model === "omega_de" ? "original" : "omega_de";
  const otherFilename = KERNEL_FILES[otherModel];
  const otherHandle = await findEntryCaseInsensitive(root, otherFilename, "file");
  if (!otherHandle) return;

  report(onProgress, {
    percent: 63,
    title: "Preparing the correct update file",
    copy: `Moving ${otherHandle.name} out of the SD-card root...`,
  });
  const file = await otherHandle.getFile();
  await preserveExistingFile(root, "ROOT", [], otherHandle.name, file);
  await root.removeEntry(otherHandle.name);
  stats.backedUp += 1;
}

async function writePackage(root, prepared, onProgress) {
  const { manifest, payload, kernelTarget } = prepared;
  for (const directory of manifest.directories || []) await getDirectory(root, directory, true);

  let written = 0;
  let preserved = 0;
  for (let index = 0; index < payload.length; index += 1) {
    const { entry, bytes } = payload[index];
    if (entry.policy === "preserve" && await pathExists(root, entry.target, "file")) {
      preserved += 1;
    } else {
      await writeFile(root, entry.target, bytes);
      written += 1;
    }
    report(onProgress, {
      percent: 64 + Math.round(((index + 1) / payload.length) * 34),
      title: "Installing DS Style",
      copy: entry.target === kernelTarget ? "Placing the kernel at the SD-card root..." : `Adding support files... (${index + 1} of ${payload.length})`,
    });
  }
  return { written, preserved };
}

export async function installDsStyle(root, model, personalisationOrProgress = null, progressCallback = null) {
  if (!KERNEL_FILES[model]) throw new Error("Choose a supported cartridge before installing DS Style.");
  const personalisation = typeof personalisationOrProgress === "function" ? null : personalisationOrProgress;
  const onProgress = typeof personalisationOrProgress === "function" ? personalisationOrProgress : progressCallback;
  const prepared = await preparePayload(model, onProgress);
  report(onProgress, { percent: 31, title: "Preparing the SD card", copy: "Checking the existing folder layout..." });
  const migration = await migrateRootFolders(root, onProgress);
  await preserveOtherKernel(root, model, migration, onProgress);
  const packageResult = await writePackage(root, prepared, onProgress);
  const appliedPreferences = personalisation ? await applyPersonalisation(root, personalisation, onProgress) : null;
  report(onProgress, { percent: 100, title: "DS Style is ready", copy: "The SD card has been prepared successfully." });
  return {
    version: prepared.manifest.version,
    kernel: prepared.kernelTarget,
    ...migration,
    ...packageResult,
    personalised: Boolean(appliedPreferences),
  };
}
