const PATH_SEPARATOR = "/";

export function supportsFileSystemAccess() {
  return typeof window.showDirectoryPicker === "function" && typeof window.showOpenFilePicker === "function";
}

export function splitPath(path) {
  return String(path || "")
    .replaceAll("\\", PATH_SEPARATOR)
    .split(PATH_SEPARATOR)
    .map((part) => part.trim())
    .filter(Boolean);
}

export async function ensurePermission(handle, mode = "readwrite") {
  if (!handle) return false;
  const options = { mode };
  if (typeof handle.queryPermission === "function") {
    const current = await handle.queryPermission(options);
    if (current === "granted") return true;
  }
  if (typeof handle.requestPermission === "function") {
    return (await handle.requestPermission(options)) === "granted";
  }
  return true;
}

export async function chooseDirectory(options = {}) {
  if (!supportsFileSystemAccess()) {
    throw new Error("This feature needs Chrome or Edge on desktop.");
  }
  return window.showDirectoryPicker({ mode: "readwrite", ...options });
}

export async function chooseFiles(options = {}) {
  if (!supportsFileSystemAccess()) {
    throw new Error("This feature needs Chrome or Edge on desktop.");
  }
  return window.showOpenFilePicker({ multiple: false, ...options });
}

export async function getDirectory(root, path, create = false) {
  let current = root;
  for (const part of splitPath(path)) {
    current = await current.getDirectoryHandle(part, { create });
  }
  return current;
}

export async function getFileHandle(root, path, create = false) {
  const parts = splitPath(path);
  const filename = parts.pop();
  if (!filename) throw new Error("A filename is required.");
  const directory = await getDirectory(root, parts.join(PATH_SEPARATOR), create);
  return directory.getFileHandle(filename, { create });
}

export async function pathExists(root, path, kind = "any") {
  try {
    if (kind === "directory") {
      await getDirectory(root, path, false);
      return true;
    }
    if (kind === "file") {
      await getFileHandle(root, path, false);
      return true;
    }
    const parts = splitPath(path);
    const name = parts.pop();
    const directory = await getDirectory(root, parts.join(PATH_SEPARATOR), false);
    for await (const [entryName] of directory.entries()) {
      if (entryName.toLocaleLowerCase() === name.toLocaleLowerCase()) return true;
    }
    return false;
  } catch {
    return false;
  }
}

export async function findEntryCaseInsensitive(directory, name, kind = "any") {
  const wanted = name.toLocaleLowerCase();
  for await (const [entryName, handle] of directory.entries()) {
    if (entryName.toLocaleLowerCase() !== wanted) continue;
    if (kind !== "any" && handle.kind !== kind) continue;
    return handle;
  }
  return null;
}

export async function writeFile(root, path, data) {
  const handle = await getFileHandle(root, path, true);
  const writable = await handle.createWritable();
  try {
    await writable.write(data);
  } finally {
    await writable.close();
  }
  return handle;
}

export async function readText(root, path) {
  const handle = await getFileHandle(root, path, false);
  return (await handle.getFile()).text();
}

export async function removeEntry(root, path, options = {}) {
  const parts = splitPath(path);
  const name = parts.pop();
  if (!name) throw new Error("An entry name is required.");
  const directory = await getDirectory(root, parts.join(PATH_SEPARATOR), false);
  await directory.removeEntry(name, options);
}

export async function listDirectory(directory, options = {}) {
  const { filesOnly = false, directoriesOnly = false, extension = "" } = options;
  const entries = [];
  const suffix = extension.toLocaleLowerCase();
  for await (const [name, handle] of directory.entries()) {
    if (filesOnly && handle.kind !== "file") continue;
    if (directoriesOnly && handle.kind !== "directory") continue;
    if (suffix && handle.kind === "file" && !name.toLocaleLowerCase().endsWith(suffix)) continue;
    entries.push({ name, handle, kind: handle.kind });
  }
  entries.sort((a, b) => {
    if (a.kind !== b.kind) return a.kind === "directory" ? -1 : 1;
    return a.name.localeCompare(b.name, undefined, { sensitivity: "base", numeric: true });
  });
  return entries;
}

export async function listFilesRecursive(root, options = {}) {
  const {
    extensions = [],
    maxDepth = 8,
    maxFiles = 2000,
    includeHidden = false,
    basePath = "",
  } = options;
  const wanted = extensions.map((value) => value.toLocaleLowerCase());
  const files = [];

  async function visit(directory, relativePath, depth) {
    if (depth > maxDepth || files.length >= maxFiles) return;
    for await (const [name, handle] of directory.entries()) {
      if (files.length >= maxFiles) break;
      if (!includeHidden && name.startsWith(".")) continue;
      const path = relativePath ? `${relativePath}/${name}` : name;
      if (handle.kind === "directory") {
        await visit(handle, path, depth + 1);
      } else if (!wanted.length || wanted.some((ext) => name.toLocaleLowerCase().endsWith(ext))) {
        files.push({ name, path, handle });
      }
    }
  }

  await visit(root, basePath, 0);
  files.sort((a, b) => a.path.localeCompare(b.path, undefined, { sensitivity: "base", numeric: true }));
  return files;
}

export async function countFiles(root, path, options = {}) {
  try {
    const directory = await getDirectory(root, path, false);
    return (await listFilesRecursive(directory, options)).length;
  } catch {
    return 0;
  }
}

export async function copyBrowserFile(root, path, file) {
  return writeFile(root, path, file);
}
