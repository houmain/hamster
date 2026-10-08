'use strict'
/* global browser, HamsterRpc */

let pageState

function localize (id, message) {
  document.getElementById(id).textContent = browser.i18n.getMessage(message)
}

function closePopup () { window.close() }

async function initialize () {
  localize('file-listing', 'menu_file_listing')
  localize('open-original', 'menu_open_original')
  localize('search', 'menu_search')
  localize('options', 'menu_options')

  pageState = await HamsterRpc.call('getActivePageState')
  const managed = !!pageState.bookmark
  document.getElementById('page-title').textContent = managed
    ? pageState.bookmark.title
    : pageState.title || pageState.url || ''
  document.getElementById('status').textContent = browser.i18n.getMessage(
    managed ? 'popup_managed_page' : 'popup_unmanaged_page')
  for (const id of ['file-listing', 'open-original']) {
    document.getElementById(id).disabled = !managed
  }

  document.getElementById('file-listing').onclick = async () => {
    await browser.tabs.create({
      url: browser.runtime.getURL('listing/listing.html?id=' + pageState.bookmark.id)
    })
    closePopup()
  }
  document.getElementById('open-original').onclick = async () => {
    await HamsterRpc.call('openOriginal', pageState.bookmark.id, pageState.url)
    closePopup()
  }
  document.getElementById('search').onclick = async () => {
    await browser.tabs.create({ url: browser.runtime.getURL('search/search.html') })
    closePopup()
  }
  document.getElementById('options').onclick = async () => {
    await browser.runtime.openOptionsPage()
    closePopup()
  }
}

document.addEventListener('DOMContentLoaded', () => {
  initialize().catch(error => {
    document.getElementById('status').textContent = error.message || String(error)
  })
})
