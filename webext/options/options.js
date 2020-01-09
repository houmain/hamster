
let backend = null
let bookmarkLibrary = null

async function updateControls () {
}

async function handleBrowseClicked () {
  let path = await Utils.getSetting('library_filesystem_root')
  const result = await backend.browserDirectories(path)
  if (result) {
    await Utils.setSetting('library_filesystem_root', result.path)
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

