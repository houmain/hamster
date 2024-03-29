'use strict'
/* global Utils, VanillaTree */

let backend
let bookmarkLibrary
let filesTree
let bookmarkUrl
let localUrl
let nextId = 1
const addedUrlIds = { }

function initializeTree () {
  const files = document.getElementById('files')
  filesTree = new VanillaTree(files, {
  })

  files.addEventListener('click', (event) => {
    const id = event.target.parentNode.getAttribute('data-vtree-id')
    if (id && event.target.tagName === 'A') {
      filesTree.toggle(id)
    }
  })
}

function splitParentBasePath (url) {
  const maxLevel = 3
  const u = new URL(url)
  if (u.pathname === '/' && !url.endsWith('/') && u.search === '' && u.hash === '') {
    return {
      parent: undefined,
      base: url,
      level: 0
    }
  }

  let level = u.pathname.match(/\//g).length - 1
  let lastSlash = u.pathname.lastIndexOf('/')
  while (level > maxLevel) {
    lastSlash = u.pathname.lastIndexOf('/', lastSlash - 1)
    --level
  }

  const parentPath = u.pathname.substr(0, lastSlash)
  const baseName = u.pathname.substr(parentPath.length + 1)
  return {
    parent: u.origin + parentPath,
    base: (baseName === '' ? '/' : baseName) + u.search + u.hash,
    level: level
  }
}

function addTreeNode (url, isLeaf, size, status) {
  let id = addedUrlIds[url]
  if (id) {
    if (size || status) {
      const node = filesTree.getLeaf(id)
      if (size) {
        node.querySelector('.size').textContent = Utils.getReadableFileSize(size)
      }
      if (status) {
        const statusNode = node.querySelector('.status')
        statusNode.textContent += (statusNode.textContent.length > 0 ? ', ' : '') + status
      }
    }
    return
  }
  id = nextId++
  addedUrlIds[url] = id

  const { parent, base, level } = splitParentBasePath(url)
  if (parent) {
    addTreeNode(parent)
  }

  if (localUrl)
    url = Utils.patchUrl(url, bookmarkUrl, localUrl)

  filesTree.add({
    label: base + '<div class="info info' + level + '">' +
      '<span class="size">' + (size ? Utils.getReadableFileSize(size) : '') + '</span>' +
      '<span class="status">' + (status || '') + '</span>' +
      '</div>',
    id: id,
    parent: addedUrlIds[parent],
    href: (isLeaf ? url : undefined),
    opened: true
  })
}

function handleRecordingEvent (event) {
  const { type, status, size, url } = (function () {
    const p = event.split(' ')
    if (p[0] === 'DOWNLOAD_FINISHED') {
      return { type: p[0], status: p[1], size: p[2], url: p[3] }
    }
    return { type: p[0], url: p[1] }
  })()
  if (type === 'STARTING' || type === 'FINISHED') {
    return
  }
  addTreeNode((url || "").trim(), true, size, type)
}

async function requestListing () {
  const urlParams = new URLSearchParams(window.location.search)
  const bookmarkId = urlParams.get('id')
  if (!bookmarkId) {
    return
  }

  const { path } = await bookmarkLibrary.getBookmarkPath(bookmarkId)
  const response = await backend.getFileListing(path)

  const urls = await bookmarkLibrary.getBookmarkUrl(bookmarkId)
  bookmarkUrl = urls.url
  localUrl = urls.localUrl

  document.getElementById('title').textContent =
    (await Utils.getBookmarkById(bookmarkId)).title

  initializeTree()

  if (response.files) {
    for (const file of response.files) {
      addTreeNode(file.url, true, file.compressedSize)
    }
  }

  bookmarkLibrary.addRecordingEventHandler(bookmarkId, handleRecordingEvent)
}

browser.runtime.getBackgroundPage().then(background => {
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()

  requestListing()
})
