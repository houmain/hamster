'use strict'
/* global browser, Utils, HamsterRpc */

function getDownloadLink (version) {
  const base = 'https://github.com/houmain/hamster/releases/download/'
  const extension = {
    'linux_x86-64': 'linux-x64.run',
    'win_x86-64': 'windows-x64.exe',
    'mac_x86-64': 'macos-x64.run'
  }[`${version.os}_${version.arch}`]
  return base + `${version.releaseVersion}/Hamster-${version.requiredVersion}-${extension}`
}

async function localizeControls () {
  Utils.localize('server-hostname-label', 'textContent', 'server_hostname')
  Utils.localize('server-port-label', 'textContent', 'server_port')
  Utils.localize('bookmark-root-parent-label', 'textContent', 'bookmark_root_parent')
  Utils.localize('bookmark-root-title-label', 'textContent', 'bookmark_root_title')
  Utils.localize('default-serve-mode-label', 'textContent', 'default_serve_mode')
  Utils.localize('bypass-hosts-label', 'textContent', 'bypass_hosts')
  Utils.localize('allow-lossy-compression-label', 'textContent', 'allow_lossy_compression')
  Utils.localize('download-ca', 'textContent', 'download_ca')
  Utils.localize('rebuild-index', 'textContent', 'rebuild_search_index')

  const options = (await Utils.getBookmarkBaseFolders())
    .map(folder => ({ value: folder.id, title: folder.title }))
  Utils.updateSelectOptions('bookmark-root-parent', options)

  const serveModes = ['latest', 'last', 'first'].map(mode => ({
    value: mode,
    title: browser.i18n.getMessage('serve_mode_' + mode)
  }))
  Utils.updateSelectOptions('default-serve-mode', serveModes)
}

function updateControls (state) {
  document.getElementById('default-serve-mode').value = state.defaultServeMode
  document.getElementById('allow-lossy-compression').checked = state.allowLossyCompression
  document.getElementById('bypass-hosts').value = state.bypassHosts
  document.getElementById('server-hostname').value = state.serverHostname
  document.getElementById('server-port').value = state.serverPort
  document.getElementById('bookmark-root-parent').value = state.rootParentId
  document.getElementById('bookmark-root-title').value = state.rootTitle
  if (state.status) updateStatus(state.status, state.serverAddress)
}

function updateStatus (status, serverAddress) {
  if (status.origin !== serverAddress) return
  document.getElementById('download-ca').href = serverAddress + '/ca.crt'
  const notification = document.getElementById('notification-panel')
  const instructions = document.getElementById('download-instructions')
  const link = document.getElementById('download-link')
  document.getElementById('controls').disabled = !status.valid
  notification.style.display = status.valid ? 'none' : 'block'
  document.getElementById('notification-details').textContent = status.detail || ''
  instructions.textContent = ''
  link.textContent = ''
  link.removeAttribute('href')
  if (!status.valid) {
    Utils.localize('notification-message', 'textContent', status.errorMessage)
    if (status.canDownload) {
      Utils.localize('download-instructions', 'textContent', 'download_instructions_' + status.os)
      link.textContent = link.href = getDownloadLink(status)
    }
  }
}

async function refreshStatus () {
  try {
    const state = await HamsterRpc.call('getOptionsState', true)
    if (state.status) updateStatus(state.status, state.serverAddress)
  } catch (error) {
    console.error(error)
  }
}

async function updateConnection () {
  try {
    updateControls(await HamsterRpc.call('setConnection',
      document.getElementById('server-hostname').value,
      document.getElementById('server-port').value))
  } catch (error) {
    const notification = document.getElementById('notification-panel')
    notification.style.display = 'block'
    const host = document.getElementById('server-hostname').value.trim().toLowerCase()
    Utils.localize('notification-message', 'textContent',
      ['127.0.0.1', 'localhost'].includes(host) ? 'notification_no_backend' : 'notification_no_server')
    document.getElementById('notification-details').textContent = error.message || String(error)
    document.getElementById('controls').disabled = true
  }
}

async function moveBookmarkRoot () {
  await HamsterRpc.call('moveBookmarkRoot', document.getElementById('bookmark-root-parent').value)
}

async function renameBookmarkRoot () {
  await HamsterRpc.call('renameBookmarkRoot', document.getElementById('bookmark-root-title').value)
}

async function updateServeMode () {
  await HamsterRpc.call('setServeMode', document.getElementById('default-serve-mode').value)
}

async function updateAllowLossyCompression () {
  await HamsterRpc.call('setAllowLossyCompression',
    document.getElementById('allow-lossy-compression').checked)
}

async function updateBypassUrls () {
  await HamsterRpc.call('setBypassHosts', document.getElementById('bypass-hosts').value)
}

async function initialize () {
  document.getElementById('server-hostname').onchange = updateConnection
  document.getElementById('server-port').onchange = updateConnection
  document.getElementById('rebuild-index').onclick = async () => {
    const button = document.getElementById('rebuild-index')
    const label = document.getElementById('index-status')
    button.disabled = true
    try {
      label.textContent = browser.i18n.getMessage('indexing')
      await HamsterRpc.call('rebuildSearchIndex')
      let status
      do {
        await Utils.sleep(250)
        status = await HamsterRpc.call('getIndexStatus')
      } while (status.indexing)
      label.textContent = browser.i18n.getMessage('search_index_ready')
    } catch (error) {
      label.textContent = error.message || String(error)
    } finally {
      button.disabled = false
    }
  }
  document.getElementById('bookmark-root-parent').onchange = moveBookmarkRoot
  document.getElementById('bookmark-root-title').onchange = renameBookmarkRoot
  document.getElementById('default-serve-mode').onchange = updateServeMode
  document.getElementById('allow-lossy-compression').onchange = updateAllowLossyCompression
  document.getElementById('bypass-hosts').onchange = updateBypassUrls

  await localizeControls()
  updateControls(await HamsterRpc.call('getOptionsState', true))
  window.setInterval(refreshStatus, 5000)
}

document.addEventListener('DOMContentLoaded', () => { initialize().catch(console.error) })
