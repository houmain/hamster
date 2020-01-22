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

  async startRecording (recorderId, path, filename, url, handleOutput) {
    const request = {
      action: 'startRecording',
      id: recorderId,
      filename: filename,
      url: url,
      path: path,
      followLink: 'P',
      validation: 'R',
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

  async setHostBlockList (list) {
    const request = {
      action: 'setHostBlockList',
      list: list
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
