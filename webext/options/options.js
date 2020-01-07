
let backend = null
let bookmarkLibrary = null

async function setSetting (key, value) {
  let settings = { }
  settings[key] = value
  return browser.storage.local.set(settings)
}

async function getSetting (key, defaultValue) {
  const settings = await browser.storage.local.get(key)
  if (settings)
    return settings[key]
  return defaultValue
}

async function updateControls () {
}

async function handleBrowseClicked () {
  let path = await getSetting('library_filesystem_root')
  const result = await backend.browserDirectories(path)
  if (result) {
    await setSetting('library_filesystem_root', result.path)
    let root = document.getElementById('library-filesystem-root')
    root.value = result.path
  }
}

;(async function () {
  const background = await browser.runtime.getBackgroundPage()
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()

  let browse = document.getElementById('library-filesystem-root-browse')
  browse.onclick = handleBrowseClicked
  document.addEventListener('DOMContentLoaded', updateControls)
})()

