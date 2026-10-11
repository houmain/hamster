(() => {
  const endpoint = document.currentScript.src;
  const reportedUrls = new Set();
  const nativeFetch = self.fetch.bind(self);
  const nativeXhrOpen = XMLHttpRequest.prototype.open;
  const nativeXhrSend = XMLHttpRequest.prototype.send;
  const xhrRequests = new WeakMap();
  let reportQueue = Promise.resolve();
  let pageLoaded = false;
  let settleTimer;
  let loadReported = false;

  const sendReport = body => {
    reportQueue = reportQueue
      .catch(() => {})
      .then(() => nativeFetch(endpoint, {
        method: "POST",
        body,
        credentials: "omit",
        cache: "no-store",
        keepalive: true
      }));
    return reportQueue;
  };

  const requestUrl = input => {
    try {
      return new URL(typeof input === "string" ? input : input.url, location.href).href;
    }
    catch (_) {
      return "";
    }
  };

  const reportRequest = url => {
    const isHttp = url.startsWith("http://") || url.startsWith("https://");
    if (!isHttp || url === endpoint || reportedUrls.has(url))
      return reportQueue;
    reportedUrls.add(url);
    return sendReport(url);
  };

  self.fetch = (input, init) => reportRequest(requestUrl(input)).then(
    () => nativeFetch(input, init),
    () => nativeFetch(input, init));

  XMLHttpRequest.prototype.open = function (method, url, async = true, ...rest) {
    xhrRequests.set(this, { url: requestUrl(url), asynchronous: async !== false });
    return nativeXhrOpen.call(this, method, url, async, ...rest);
  };

  XMLHttpRequest.prototype.send = function (body) {
    const send = () => nativeXhrSend.call(this, body);
    const request = xhrRequests.get(this);
    if (!request || !request.asynchronous)
      return send();
    reportRequest(request.url).then(send, send);
  };

  const reportResources = entries => {
    const newUrls = [];
    for (const entry of entries) {
      const url = entry.name;
      const isHttp = url.startsWith("http://") || url.startsWith("https://");
      if (isHttp && url !== endpoint && !reportedUrls.has(url)) {
        reportedUrls.add(url);
        newUrls.push(url);
      }
    }
    if (newUrls.length)
      sendReport(newUrls.join("\n"));
    return newUrls.length > 0;
  };

  const scheduleLoadedReport = () => {
    clearTimeout(settleTimer);
    settleTimer = setTimeout(() => {
      if (reportResources(performance.getEntriesByType("resource"))) {
        scheduleLoadedReport();
        return;
      }
      if (!loadReported) {
        loadReported = true;
        sendReport("@loaded");
      }
    }, 500);
  };

  try {
    new PerformanceObserver(entryList => {
      reportResources(entryList.getEntries());
      if (pageLoaded)
        scheduleLoadedReport();
    }).observe({ type: "resource", buffered: true });
    reportResources(performance.getEntriesByType("resource"));
    if (self === top) {
      addEventListener("load", () => {
        pageLoaded = true;
        scheduleLoadedReport();
      }, { once: true });
    }
  }
  catch (_) {}
})();
