
let backend = undefined
let bookmarkLibrary = undefined
let restoreOptions = undefined

function localizeControls () {
  Utils.localize('bookmark-root-parent-label', 'innerText', 'bookmark_root_parent')
  Utils.localize('bookmark-root-title-label', 'innerText', 'bookmark_root_title')
  Utils.localize('filesystem-root-label', 'innerText', 'filesystem_root')
  Utils.localize('filesystem-root-browse', 'innerText', 'filesystem_root_browse')
  Utils.localize('default-refresh-mode-label', 'innerText', 'default_refresh_mode')
  Utils.localize('allow-lossy-compression-label', 'innerText', 'allow_lossy_compression')

  const refreshModes = []
  for (const mode of ['standard', 'async', 'never'])
    refreshModes.push({ value: mode, title: browser.i18n.getMessage('refresh_mode_' + mode) })
  Utils.updateSelectOptions('default-refresh-mode', refreshModes)
}

async function updateControls () {
  const options = []
  for (const folder of await Utils.getBookmarkBaseFolders()) {
    options.push({
      value: folder.id,
      title: folder.title
    })
  }
  Utils.updateSelectOptions('bookmark-root-parent', options)

  const root = await Utils.getBookmarkById(bookmarkLibrary.rootId)
  const rootParent = document.getElementById('bookmark-root-parent')
  rootParent.value = root.parentId
  const rootTitle = document.getElementById('bookmark-root-title')
  rootTitle.value = root.title

  const filesystemRoot = document.getElementById('filesystem-root')
  filesystemRoot.value = backend.filesystemRoot

  document.getElementById('default-refresh-mode').value = await Utils.getSetting('default-refresh-mode')
  document.getElementById('allow-lossy-compression').checked = await Utils.getSetting('allow-lossy-compression')
}

async function moveBookmarkRoot () {
  const parentId = document.getElementById('bookmark-root-parent').value
  return browser.bookmarks.move(bookmarkLibrary.rootId, { parentId: parentId })
}

async function renameBookmarkRoot () {
  const title = document.getElementById('bookmark-root-title').value
  return browser.bookmarks.update(bookmarkLibrary.rootId, { title: title })
}

async function browseFilesystemRoot () {
  const result = await backend.browserDirectories(backend.filesystemRoot)
  if (result.path) {
    await Utils.setSetting('filesystem-root', result.path)
    await restoreOptions()
    return updateControls()
  }
}

async function updateRefreshMode() {
  const value = document.getElementById('default-refresh-mode').value
  return Utils.setSetting('default-refresh-mode', value)
}

async function updateAllowLossyCompression(e) {
  const checked = document.getElementById('allow-lossy-compression').checked
  return Utils.setSetting('allow-lossy-compression', checked)
}

async function initialize() {
  const background = await browser.runtime.getBackgroundPage()
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()
  restoreOptions = background.restoreOptions

  document.getElementById('bookmark-root-parent').onchange = moveBookmarkRoot
  document.getElementById('bookmark-root-title').onchange = renameBookmarkRoot
  document.getElementById('filesystem-root-browse').onclick = browseFilesystemRoot
  document.getElementById('default-refresh-mode').onchange = updateRefreshMode
  document.getElementById('allow-lossy-compression').onchange = updateAllowLossyCompression

  await restoreOptions()
  localizeControls()
  updateControls()
}

document.addEventListener('DOMContentLoaded', initialize)
