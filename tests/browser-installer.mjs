import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { File } from "node:buffer";
import { fileURLToPath } from "node:url";

globalThis.File ||= File;
const nativeFetch = globalThis.fetch;
globalThis.fetch = async (input, options) => {
  const url = input instanceof URL ? input : new URL(input);
  if (url.protocol !== "file:") return nativeFetch(input, options);
  try {
    const fileUrl = new URL(url);
    fileUrl.search = "";
    fileUrl.hash = "";
    return new Response(await readFile(fileURLToPath(fileUrl)), { status: 200 });
  } catch {
    return new Response("Not found", { status: 404 });
  }
};

class MemoryFileHandle {
  kind = "file";

  constructor(name, bytes = new Uint8Array()) {
    this.name = name;
    this.bytes = Uint8Array.from(bytes);
  }

  async getFile() {
    return new File([this.bytes], this.name);
  }

  async createWritable() {
    let next = this.bytes;
    return {
      write: async (data) => {
        if (data instanceof Blob) next = new Uint8Array(await data.arrayBuffer());
        else if (data instanceof ArrayBuffer) next = new Uint8Array(data);
        else if (ArrayBuffer.isView(data)) next = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
        else next = new TextEncoder().encode(String(data));
      },
      close: async () => {
        this.bytes = Uint8Array.from(next);
      },
    };
  }
}

class MemoryDirectoryHandle {
  kind = "directory";

  constructor(name) {
    this.name = name;
    this.children = new Map();
  }

  async *entries() {
    yield* this.children.entries();
  }

  async getDirectoryHandle(name, options = {}) {
    const existing = this.children.get(name);
    if (existing?.kind === "directory") return existing;
    if (existing || !options.create) throw Object.assign(new Error("Directory not found"), { name: "NotFoundError" });
    const directory = new MemoryDirectoryHandle(name);
    this.children.set(name, directory);
    return directory;
  }

  async getFileHandle(name, options = {}) {
    const existing = this.children.get(name);
    if (existing?.kind === "file") return existing;
    if (existing || !options.create) throw Object.assign(new Error("File not found"), { name: "NotFoundError" });
    const file = new MemoryFileHandle(name);
    this.children.set(name, file);
    return file;
  }

  async removeEntry(name, options = {}) {
    const existing = this.children.get(name);
    if (!existing) throw Object.assign(new Error("Entry not found"), { name: "NotFoundError" });
    if (existing.kind === "directory" && existing.children.size && !options.recursive) {
      throw Object.assign(new Error("Directory is not empty"), { name: "InvalidModificationError" });
    }
    this.children.delete(name);
  }
}

function byteString(value) {
  return new TextEncoder().encode(value);
}

async function directoryAt(root, path, create = false) {
  let current = root;
  for (const part of path.split("/").filter(Boolean)) current = await current.getDirectoryHandle(part, { create });
  return current;
}

async function seed(root, path, value) {
  const parts = path.split("/");
  const name = parts.pop();
  const parent = await directoryAt(root, parts.join("/"), true);
  parent.children.set(name, new MemoryFileHandle(name, typeof value === "string" ? byteString(value) : value));
}

async function fileAt(root, path) {
  const parts = path.split("/");
  const name = parts.pop();
  const parent = await directoryAt(root, parts.join("/"), false);
  return parent.getFileHandle(name, { create: false });
}

async function exists(root, path) {
  try {
    const parts = path.split("/");
    const name = parts.pop();
    const parent = await directoryAt(root, parts.join("/"), false);
    return parent.children.has(name);
  } catch {
    return false;
  }
}

async function textAt(root, path) {
  return new TextDecoder().decode((await fileAt(root, path)).bytes);
}

const moduleUrl = new URL("../docs/manager/js/installer.js", import.meta.url);
const { installBundledStyle, installDsStyle, loadPersonalisation, savePersonalisation } = await import(moduleUrl.href);

{
  const root = new MemoryDirectoryHandle("blank-de");
  const result = await installDsStyle(root, "omega_de", {
    name: "FrankieT19",
    theme: "Dark",
    colour: "Purple",
    language: "Español",
    startScreen: "On",
    startSource: "Favourites",
    boot: "SD",
    viewMode: "List + art",
    listArt: "Top",
    thumbnails: "Box",
    artBorder: "Accent",
    roundedCorners: "Full",
    verticalSide: "Left",
    horizontalSide: "Bottom",
    hideSystemFiles: "On",
    listFolders: "On",
    cleanList: "On",
    clock: "12 hour",
    sounds: "Off",
    quickStart: "L",
  });
  assert.equal(result.version, "7.3");
  assert.equal(result.personalised, true);
  assert.equal(await exists(root, "ezkernelnew.bin"), true);
  assert.equal(await exists(root, "ezkernel.bin"), false);
  assert.equal(await exists(root, "SYSTEM/NAME.TXT"), true);
  assert.equal(await exists(root, "SYSTEM/IMGS/CUSTOM"), true);
  assert.equal(await exists(root, "SYSTEM/IMGS2/CUSTOM"), true);
  assert.match(await textAt(root, "SYSTEM/NAME.TXT"), /^FrankieT19\r?\n/);
  const settings = await textAt(root, "SYSTEM/SETTINGS.TXT");
  assert.match(settings, /^Theme = Dark$/m);
  assert.match(settings, /^Colour = Purple$/m);
  assert.match(settings, /^Language = Español$/m);
  assert.match(settings, /^Start screen source = Favourites$/m);
  assert.match(settings, /^Boot to = SD$/m);
  assert.match(settings, /^View mode = List \+ art$/m);
  assert.match(settings, /^List art = Top$/m);
  assert.match(settings, /^Thumbnails = Box$/m);
  assert.match(settings, /^Art border = Accent$/m);
  assert.match(settings, /^Rounded corners = Full$/m);
  assert.match(settings, /^Vertical side = Left$/m);
  assert.match(settings, /^Horizontal side = Bottom$/m);
  assert.match(settings, /^List folders = On$/m);
  assert.match(settings, /^Clean list = On$/m);
  assert.match(settings, /^Clock format = 12 hour$/m);
  assert.match(settings, /^Sounds = Off$/m);
  assert.match(settings, /^Quick start hotkey = L$/m);

  const style = await installBundledStyle(root, "omega_de", "analogue");
  assert.equal(style.label, "Analogue Style");
  assert.equal(await exists(root, "SYSTEM/KERNELS/Analogue Style v7.2.bin"), true);
  const simpleStyle = await installBundledStyle(root, "omega_de", "simple");
  assert.equal(simpleStyle.label, "Simple Style");
  assert.equal(await exists(root, "SYSTEM/KERNELS/Simple Style v7.2.bin"), true);
}

{
  const root = new MemoryDirectoryHandle("stock-original");
  await seed(root, "ezkernel.bin", "old kernel");
  await seed(root, "SAVER/Mario.sav", "save data");
  await seed(root, "RTS/Mario.rts", "state data");
  await seed(root, "SYSTEM/NAME.TXT", "My existing name");
  const result = await installDsStyle(root, "original");
  assert.equal(await textAt(root, "SYSTEM/NAME.TXT"), "My existing name");
  assert.equal(await textAt(root, "SYSTEM/SAVER/Mario.sav"), "save data");
  assert.equal(await textAt(root, "SYSTEM/RTS/Mario.rts"), "state data");
  assert.equal(await exists(root, "SAVER"), false);
  assert.equal(await exists(root, "RTS"), false);
  assert.equal(await exists(root, "ezkernel.bin"), true);
  assert.equal(result.preserved, 1);
  await installBundledStyle(root, "original", "analogue");
  await installBundledStyle(root, "original", "simple");
  assert.equal(await exists(root, "SYSTEM/KERNELS/Analogue Style v7.2.bin"), true);
  assert.equal(await exists(root, "SYSTEM/KERNELS/Simple Style v7.2.bin"), true);
}

{
  const root = new MemoryDirectoryHandle("simple-conflict");
  await seed(root, "SAVER/Game.sav", "root version");
  await seed(root, "SYSTEM/SAVER/Game.sav", "system version");
  const result = await installDsStyle(root, "omega_de");
  assert.equal(await textAt(root, "SYSTEM/SAVER/Game.sav"), "root version");
  assert.equal(await textAt(root, "SYSTEM/BACKUP/WEB INSTALL BACKUP/SAVER/Game.sav"), "system version");
  assert.equal(result.backedUp, 1);
}

{
  const root = new MemoryDirectoryHandle("ambiguous");
  await seed(root, "ezkernel.bin", "original kernel");
  await seed(root, "ezkernelnew.bin", "old DE kernel");
  const result = await installDsStyle(root, "omega_de");
  assert.equal(await exists(root, "ezkernel.bin"), false);
  assert.equal(await textAt(root, "SYSTEM/BACKUP/WEB INSTALL BACKUP/ROOT/ezkernel.bin"), "original kernel");
  assert.equal(await exists(root, "ezkernelnew.bin"), true);
  assert.equal(result.backedUp, 1);
}

{
  const root = new MemoryDirectoryHandle("existing-ds-style");
  await seed(root, "SYSTEM/IMGS/CUSTOM/Homebrew.bmp", "artwork");
  await seed(root, "SYSTEM/SAVER/Progress.sav", "progress");
  await seed(root, "SYSTEM/NAME.TXT", "Frankie");
  await installDsStyle(root, "omega_de");
  assert.equal(await textAt(root, "SYSTEM/IMGS/CUSTOM/Homebrew.bmp"), "artwork");
  assert.equal(await textAt(root, "SYSTEM/SAVER/Progress.sav"), "progress");
  assert.equal(await textAt(root, "SYSTEM/NAME.TXT"), "Frankie");
}

{
  const root = new MemoryDirectoryHandle("existing-settings");
  await seed(root, "SYSTEM/NAME.TXT", "Old name\r\n# Keep this note\r\n");
  await seed(root, "SYSTEM/SETTINGS.TXT", [
    "# Existing settings",
    "Theme = Dark",
    "Language = Fran\u00e7ais",
    "Colour = Red",
    "Theme = Light",
    "View mode = Vertical",
    "",
  ].join("\r\n"));
  const loaded = await loadPersonalisation(root);
  assert.equal(loaded.name, "Old name");
  assert.equal(loaded.theme, "Light");
  assert.equal(loaded.colour, "Red");
  assert.equal(loaded.language, "Français");
  assert.equal(loaded.viewMode, "Vertical");
  assert.equal(loaded.boot, "Start");
  await installDsStyle(root, "omega_de", {
    ...loaded,
    name: "New name",
    theme: "Light",
    colour: "Green",
    boot: "Favourites",
    clock: "24 hour",
    sounds: "On",
  });
  const settings = await textAt(root, "SYSTEM/SETTINGS.TXT");
  assert.match(settings, /Language = Fran\u00e7ais/);
  assert.match(settings, /^View mode = Vertical$/m);
  assert.equal((settings.match(/^Theme = /gm) || []).length, 1);
  assert.match(settings, /^Theme = Light$/m);
  assert.match(settings, /^Colour = Green$/m);
  assert.match(settings, /^Boot to = Favourites$/m);
  assert.equal(settings.includes("\r\n"), true);
  const name = await textAt(root, "SYSTEM/NAME.TXT");
  assert.match(name, /^New name\r\n/);
  assert.match(name, /# Keep this note/);
}

{
  const root = new MemoryDirectoryHandle("save-settings-only");
  await savePersonalisation(root, {
    name: "Manager",
    language: "ภาษาไทย",
    viewMode: "Horizontal",
    roundedCorners: "No Start",
  });
  assert.match(await textAt(root, "SYSTEM/NAME.TXT"), /^Manager\n/);
  const settings = await textAt(root, "SYSTEM/SETTINGS.TXT");
  assert.match(settings, /^Language = ภาษาไทย$/m);
  assert.match(settings, /^Rounded corners = No Start$/m);
}

console.log("Browser installer scenarios passed.");
