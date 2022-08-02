'use strict'
/* global browser, Utils */

let backend
let bookmarkLibrary
let restoreOptions

function getDownloadLink (version) {
  const base = 'https://github.com/houmain/hamster/releases/download/'
  const extension = {
    'linux_x86-64': 'linux-x64.run',
    'win_x86-64': 'windows-x64.msi',
    'mac_x86-64': 'macos-x64.run'
  }[`${version.os}_${version.arch}`]
  return base + `${version.requiredVersion}.0/BookmarkHamster-${version.requiredVersion}-${extension}`
}

async function localizeControls () {
  Utils.localize('bookmark-root-parent-label', 'textContent', 'bookmark_root_parent')
  Utils.localize('bookmark-root-title-label', 'textContent', 'bookmark_root_title')
  Utils.localize('filesystem-root-label', 'textContent', 'filesystem_root')
  Utils.localize('filesystem-root-browse', 'textContent', 'filesystem_root_browse')
  Utils.localize('default-serve-mode-label', 'textContent', 'default_serve_mode')
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

  const serveModes = []
  for (const mode of ['latest', 'last', 'first']) {
    serveModes.push({ value: mode, title: browser.i18n.getMessage('serve_mode_' + mode) })
  }
  Utils.updateSelectOptions('default-serve-mode', serveModes)
}

async function updateControls () {
  document.getElementById('default-serve-mode').value = await Utils.getSetting('default-serve-mode')
  document.getElementById('allow-lossy-compression').checked = await Utils.getSetting('allow-lossy-compression')
  document.getElementById('bypass-hosts').textContent = await Utils.getSetting('bypass-hosts')

  const version = await backend.getVersion()
  document.getElementById('controls').disabled = (!version.valid)
  document.getElementById('notification-panel').style.visibility = (version.valid ? 'collapse' : 'visible')
  if (version.valid) {
    const root = await Utils.getBookmarkById(bookmarkLibrary.rootId)
    const rootParent = document.getElementById('bookmark-root-parent')
    rootParent.value = root.parentId
    const rootTitle = document.getElementById('bookmark-root-title')
    rootTitle.value = root.title

    const filesystemRoot = document.getElementById('filesystem-root')
    filesystemRoot.value = backend.filesystemRoot
  } else {
    Utils.localize('notification-message', 'textContent', version.errorMessage)
    if (version.supported) {
      Utils.localize('download-instructions', 'textContent', 'download_instructions_' + version.os)
    } else {
      document.getElementById('download-instructions').textContent =
        `os='${version.os}' arch='${version.arch}'`
    }
    const link = document.getElementById('download-link')
    link.textContent = link.href = getDownloadLink(version)
  }
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

async function updateServeMode () {
  const value = document.getElementById('default-serve-mode').value
  return Utils.setSetting('default-serve-mode', value)
}

async function updateAllowLossyCompression (e) {
  const checked = document.getElementById('allow-lossy-compression').checked
  return Utils.setSetting('allow-lossy-compression', checked)
}

async function updateBypassUrls (e) {
  const hostList = document.getElementById('bypass-hosts').value
  await Utils.setSetting('bypass-hosts', hostList)
  await bookmarkLibrary.setBypassHosts(hostList)
}

async function initialize () {
  const background = await browser.runtime.getBackgroundPage()
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()
  restoreOptions = background.restoreOptions

  document.getElementById('bookmark-root-parent').onchange = moveBookmarkRoot
  document.getElementById('bookmark-root-title').onchange = renameBookmarkRoot
  document.getElementById('filesystem-root-browse').onclick = browseFilesystemRoot
  document.getElementById('default-serve-mode').onchange = updateServeMode
  document.getElementById('allow-lossy-compression').onchange = updateAllowLossyCompression
  document.getElementById('bypass-hosts').onchange = updateBypassUrls

  await localizeControls()
  await restoreOptions()
  await updateControls()
}

document.addEventListener('DOMContentLoaded', initialize)
