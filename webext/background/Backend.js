'use strict'

class Backend {

  constructor (nativeClient) {
    this._filesystemRoot = undefined
    this._nativeClient = nativeClient
    this._nativeClient.addConnectionHandler(() => this.setFilesystemRoot())
  }

  async setFilesystemRoot (filesystemRoot) {
    const request = {
      action: 'setLibraryRoot',
      path: (filesystemRoot || this._filesystemRoot)
    }
    const response = await this._nativeClient.sendRequest(request)
    if (response) {
      this._filesystemRoot = response.path
    }
  }

  get filesystemRoot () {
    return this._filesystemRoot
  }

  async startRecording (recorderId, path, url, handleOutput) {
    const request = {
      action: 'startRecording',
      id: recorderId,
      url: url,
      path: path,
      followLink: 'P',
      validation: 'R',
      allowLossyCompression: true
    }
    await this._nativeClient.sendRequest(request)
    this._pollRecordingOutput(recorderId, handleOutput)
  }

  async stopRecording (recorderId) {
    const request = {
      action: 'stopRecording',
      id: recorderId
    }
    return this._nativeClient.sendRequest(request)
  }

  async getFileSize (path) {
    const request = {
      action: 'getFileSize',
      path: path
    }
    return this._nativeClient.sendRequest(request)
  }

  async moveFile (sourcePath, targetPath) {
    const request = {
      action: 'moveFile',
      from: sourcePath,
      to: targetPath
    }
    return this._nativeClient.sendRequest(request)
  }

  async deleteFile (path, undeleteId) {
    const request = {
      action: 'deleteFile',
      path: path,
      undeleteId: undeleteId
    }
    return this._nativeClient.sendRequest(request)
  }

  async undeleteFile (undeleteId) {
    const request = {
      action: 'undeleteFile',
      undeleteId: undeleteId
    }
    return this._nativeClient.sendRequest(request)
  }

  async browserDirectories (initialPath) {
    const request = {
      action: 'browserDirectories',
      path: initialPath
    }
    return this._nativeClient.sendRequest(request)
  }

  async setHostBlockList (list, append) {
    const request = {
      action: 'setHostBlockList',
      list: list,
      append: append
    }
    return this._nativeClient.sendRequest(request)
  }

  async updateSearchIndex (path) {
    const request = {
      action: 'updateSearchIndex',
      path: path
    }
    return this._nativeClient.sendRequest(request)
  }

  async executeSearch (query, forSearchPage) {
    // replace space with *
    query = (query + ' ').replace(/\s+/g, '*')

    const request = {
      action: 'executeSearch',
      query: query
    }
    if (forSearchPage) {
      request.highlight = true
      request.snippetSize = 32
      request.maxCount = 20
    }
    return this._nativeClient.sendRequest(request)
  }

  async _pollRecordingOutput (recorderId, handleOutput) {
    const request = {
      action: 'getRecordingOutput',
      id: recorderId
    }
    const response = await this._nativeClient.sendRequest(request)
    if (response.events) {
      for (const event of response.events) {
        await handleOutput(event)
      }
      setTimeout(() => this._pollRecordingOutput(recorderId, handleOutput), 500)
    } else {
      await handleOutput()
    }
  }
}
