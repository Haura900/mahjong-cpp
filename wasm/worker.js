let modulePromise;
const assetVersion = new URL(import.meta.url).searchParams.get("v") || "1";

function getModule() {
  modulePromise ||= import(`./mahjong.js?v=${encodeURIComponent(assetVersion)}`)
    .then(({ default: createMahjongModule }) => createMahjongModule({
      locateFile: (path) => {
        const url = new URL(path, import.meta.url);
        url.searchParams.set("v", assetVersion);
        return url.href;
      },
    }));
  return modulePromise;
}

self.onmessage = async (event) => {
  const { id, payload } = event.data;
  try {
    const module = await getModule();
    const result = JSON.parse(module.analyzeJson(JSON.stringify(payload)));
    self.postMessage({ id, result, engineVersion: module.engineVersion() });
  } catch (error) {
    self.postMessage({ id, error: error?.message || String(error) });
  }
};
