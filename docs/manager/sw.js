const CACHE_NAME = "ds-style-manager-v6";
const CORE_ASSETS = [
  "./",
  "./index.html",
  "./styles.css",
  "./js/app.js",
  "./js/artwork.js",
  "./js/filesystem.js",
  "./js/images.js",
  "./js/installer.js",
  "./js/preview.js",
  "./assets/preview/font.bin",
  "./assets/preview/languages.json",
  "./assets/preview/backgrounds/light/start.png",
  "./assets/preview/backgrounds/light/sd_list.png",
  "./assets/preview/backgrounds/light/sd_horizontal.png",
  "./assets/preview/backgrounds/light/sd_vertical.png",
  "./assets/preview/backgrounds/dark/start.png",
  "./assets/preview/backgrounds/dark/sd_list.png",
  "./assets/preview/backgrounds/dark/sd_horizontal.png",
  "./assets/preview/backgrounds/dark/sd_vertical.png",
  "./assets/preview/topbars/pale_blue.png",
  "./assets/preview/topbars/light_blue.png",
  "./assets/preview/topbars/blue.png",
  "./assets/preview/topbars/dark_blue.png",
  "./assets/preview/topbars/green.png",
  "./assets/preview/topbars/pale_green.png",
  "./assets/preview/topbars/bright_green.png",
  "./assets/preview/topbars/lime.png",
  "./assets/preview/topbars/yellow.png",
  "./assets/preview/topbars/red.png",
  "./assets/preview/topbars/orange.png",
  "./assets/preview/topbars/brown.png",
  "./assets/preview/topbars/pink.png",
  "./assets/preview/topbars/pale_pink.png",
  "./assets/preview/topbars/magenta.png",
  "./assets/preview/topbars/purple.png",
  "./assets/preview/icons/pale_blue/folder.png",
  "./assets/preview/icons/pale_blue/gba.png",
  "./assets/preview/icons/light_blue/folder.png",
  "./assets/preview/icons/light_blue/gba.png",
  "./assets/preview/icons/blue/folder.png",
  "./assets/preview/icons/blue/gba.png",
  "./assets/preview/icons/dark_blue/folder.png",
  "./assets/preview/icons/dark_blue/gba.png",
  "./assets/preview/icons/green/folder.png",
  "./assets/preview/icons/green/gba.png",
  "./assets/preview/icons/pale_green/folder.png",
  "./assets/preview/icons/pale_green/gba.png",
  "./assets/preview/icons/bright_green/folder.png",
  "./assets/preview/icons/bright_green/gba.png",
  "./assets/preview/icons/lime/folder.png",
  "./assets/preview/icons/lime/gba.png",
  "./assets/preview/icons/yellow/folder.png",
  "./assets/preview/icons/yellow/gba.png",
  "./assets/preview/icons/red/folder.png",
  "./assets/preview/icons/red/gba.png",
  "./assets/preview/icons/orange/folder.png",
  "./assets/preview/icons/orange/gba.png",
  "./assets/preview/icons/brown/folder.png",
  "./assets/preview/icons/brown/gba.png",
  "./assets/preview/icons/pink/folder.png",
  "./assets/preview/icons/pink/gba.png",
  "./assets/preview/icons/pale_pink/folder.png",
  "./assets/preview/icons/pale_pink/gba.png",
  "./assets/preview/icons/magenta/folder.png",
  "./assets/preview/icons/magenta/gba.png",
  "./assets/preview/icons/purple/folder.png",
  "./assets/preview/icons/purple/gba.png",
  "./assets/preview/artwork/amke-wide.png",
  "./assets/preview/artwork/amke-square.png",
  "./assets/preview/artwork/bmge-wide.png",
  "./assets/preview/artwork/bmge-square.png",
  "./assets/preview/artwork/bpee-wide.png",
  "./assets/preview/artwork/a2ae-square.png",
  "./manifest.webmanifest",
  "../project/assets/Logo.png",
  "../project/assets/favicon.png"
];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE_NAME).then((cache) => cache.addAll(CORE_ASSETS)));
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (event.request.method !== "GET" || url.origin !== self.location.origin) return;
  if (url.pathname.includes("/manager/packages/")) {
    event.respondWith(fetch(event.request, { cache: "no-store" }));
    return;
  }
  event.respondWith(
    fetch(event.request)
      .then((response) => {
        const copy = response.clone();
        caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copy));
        return response;
      })
      .catch(() => caches.match(event.request))
  );
});
