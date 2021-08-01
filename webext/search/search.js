'use strict'

let backend

async function executeSearch () {
  const urlParams = new URLSearchParams(window.location.search)
  const query = urlParams.get('s')
  if (!query) {
    return
  }
  const response = await backend.executeSearch(query, true)
  const results = document.createDocumentFragment()

  if (response.matches.length > 0) {
    for (const match of response.matches) {
      const result = document.createElement('div')
      result.classList.add('result')

      const titleDiv = document.createElement('div')
      const titleLink = document.createElement('a')
      titleLink.setAttribute('href', match.url)
      const title = document.createElement('h2')
      title.classList.add('nooverflow')
      title.textContent = match.title
      titleLink.appendChild(title)
      titleDiv.appendChild(titleLink)
      result.appendChild(titleDiv)

      const urlDiv = document.createElement('div')
      urlDiv.classList.add('nooverflow')
      const urlLink = document.createElement('a')
      urlLink.textContent = match.url
      urlLink.setAttribute('href', match.url)
      urlDiv.appendChild(urlLink)
      result.appendChild(urlDiv)

      const snippetDiv = document.createElement('div')
      for (let pos = 0; ;) {
        const begin = match.snippet.indexOf('<b>', pos)
        if (begin >= 0) {
          const end = match.snippet.indexOf('</b>', begin + 3)
          if (end >= 0) {
            snippetDiv.appendChild(document.createTextNode(
              match.snippet.substring(pos, begin)
            ))
            const bold = document.createElement('b')
            bold.textContent = match.snippet.substring(begin + 3, end)
            snippetDiv.appendChild(bold)
            pos = end + 4
            continue
          }
        }
        snippetDiv.appendChild(document.createTextNode(
          match.snippet.substring(pos)
        ))
        break
      }
      result.appendChild(snippetDiv)

      results.appendChild(result)
    }
  }
  else {
    results.textContent = browser.i18n.getMessage('search_no_results')
  }
  const resultsElement = document.getElementById('results')
  resultsElement.textContent = ''
  resultsElement.appendChild(results)
}

browser.runtime.getBackgroundPage().then(background => {
  backend = background.getBackend()

  document.addEventListener('DOMContentLoaded', executeSearch)
})

document.getElementById('search-input').value =
  new URLSearchParams(window.location.search).get('s')
document.getElementById('search-input').placeholder =
  browser.i18n.getMessage('search_placeholder')
document.getElementById('search-submit').value =
  browser.i18n.getMessage('search_submit')

document.getElementById('search-input').focus()
