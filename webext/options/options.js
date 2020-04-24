'use strict'

let backend = undefined
let bookmarkLibrary = undefined
let restoreOptions = undefined

async function localizeControls () {
  Utils.localize('bookmark-root-parent-label', 'textContent', 'bookmark_root_parent')
  Utils.localize('bookmark-root-title-label', 'textContent', 'bookmark_root_title')
  Utils.localize('filesystem-root-label', 'textContent', 'filesystem_root')
  Utils.localize('filesystem-root-browse', 'textContent', 'filesystem_root_browse')
  Utils.localize('default-refresh-mode-label', 'textContent', 'default_refresh_mode')
  Utils.localize('bypass-hosts-label', 'textContent', 'bypass_hosts')
  Utils.localize('allow-lossy-compression-label', 'textContent', 'allow_lossy_compression')

  const options = []
  for (const folder of await Utils.getBookmarkBaseFolders()) {
    options.push({
      value: folder.id,
      title: folder.title
    })
  }
  Utils.updateSelectOptions('bookmark-root-parent', options)

  const refreshModes = []
  for (const mode of ['standard', 'async', 'never']) {
    refreshModes.push({ value: mode, title: browser.i18n.getMessage('refresh_mode_' + mode) })
  }
  Utils.updateSelectOptions('default-refresh-mode', refreshModes)
}

async function updateControls () {
  const root = await Utils.getBookmarkById(bookmarkLibrary.rootId)
  const rootParent = document.getElementById('bookmark-root-parent')
  rootParent.value = root.parentId
  const rootTitle = document.getElementById('bookmark-root-title')
  rootTitle.value = root.title

  const filesystemRoot = document.getElementById('filesystem-root')
  filesystemRoot.value = backend.filesystemRoot

  document.getElementById('default-refresh-mode').value = await Utils.getSetting('default-refresh-mode')
  document.getElementById('allow-lossy-compression').checked = await Utils.getSetting('allow-lossy-compression')
  document.getElementById('bypass-hosts').textContent = await Utils.getSetting('bypass-hosts')
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

async function updateBypassUrls(e) {
  const text = document.getElementById('bypass-hosts').value
  console.log(text)
  return Utils.setSetting('bypass-hosts', text)
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
  document.getElementById('bypass-hosts').onchange = updateBypassUrls

  localizeControls()
  await restoreOptions()
  await updateControls()
}

document.addEventListener('DOMContentLoaded', initialize)
