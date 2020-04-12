
let backend = undefined
let bookmarkLibrary = undefined

async function executeSearch () {
  const urlParams = new URLSearchParams(window.location.search)
  const query = urlParams.get('s')
  if (query) {
    let html = ''
    const response = await backend.executeSearch(query, true)
    const matches = response.matches
    if (matches.length > 0) {
      for (const match of matches) {
        html += '<div class="result">'
        html += '<div><a href="' + match.url + '"><h2 class="nooverflow">' + match.title + '</h2></a></div>'
        html += '<div class="nooverflow"><a href="' + match.url + '">' + match.url + '</a></div>'
        html += '<div>' + match.snippet + '</div>'
        html += '</div>'
      }
    }
    else {
      html = browser.i18n.getMessage('search_no_results')
    }
    const results = document.getElementById('results')
    results.innerHTML = html
  }
}

browser.runtime.getBackgroundPage().then(background => {
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()

  document.addEventListener('DOMContentLoaded', executeSearch)
})
