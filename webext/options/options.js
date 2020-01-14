
let backend = null
let bookmarkLibrary = null

async function updateControls () {
  const options = []
  for (const folder of await Utils.getBookmarkBaseFolders()) {
    options.push({
      value: folder.id,
      title: folder.title
    })
  }
  Utils.updateSelectOptions('bookmark-library-root-parent', options)

  const path = await getSetting('library_filesystem_root')
  const root = document.getElementById('library-filesystem-root')
  root.value = path
}

async function handleBrowseClicked () {
  const path = await Utils.getSetting('library_filesystem_root')
  const result = await backend.browserDirectories(path)
  if (result.path) {
    await Utils.setSetting('library_filesystem_root', result.path)
    const root = document.getElementById('library-filesystem-root')
    root.value = result.path
  }
}

;(async function () {
  const background = await browser.runtime.getBackgroundPage()
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()

  const browse = document.getElementById('library-filesystem-root-browse')
  browse.onclick = handleBrowseClicked
  document.addEventListener('DOMContentLoaded', updateControls)
})()

