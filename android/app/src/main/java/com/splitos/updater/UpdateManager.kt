package com.splitos.updater

import android.content.Context
import android.util.Log
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/**
 * SplitOS GitHub In-App Updater & Image Downloader.
 * Checks GitHub Releases API for new guest OS images (guest_os.bin)
 * and host APK updates, allowing in-place guest OS upgrades.
 */
class UpdateManager(private val context: Context) {

    companion object {
        private const val TAG = "UpdateManager"
        private const val GITHUB_REPO = "your-username/split-os"
        private const val RELEASES_API_URL = "https://api.github.com/repos/$GITHUB_REPO/releases/latest"
        private const val GUEST_OS_FILE = "guest_os.bin"
        private const val MAX_IMAGE_BYTES = 16 * 1024 * 1024L

        internal fun validateGuestImage(file: File) {
            require(file.length() in 32..MAX_IMAGE_BYTES) { "Invalid guest image size" }
            val magic = ByteArray(8)
            file.inputStream().use { java.io.DataInputStream(it).readFully(magic) }
            require(magic.contentEquals("SPLITOS\u0000".toByteArray(Charsets.US_ASCII))) {
                "Invalid guest image header"
            }
        }
    }

    data class ReleaseInfo(
        val tagName: String,
        val releaseName: String,
        val releaseNotes: String,
        val guestOsAssetUrl: String?,
        val apkAssetUrl: String?
    )

    interface UpdateCallback {
        fun onUpdateAvailable(info: ReleaseInfo)
        fun onNoUpdateAvailable()
        fun onDownloadProgress(percent: Int)
        fun onUpdateComplete(file: File)
        fun onError(message: String)
    }

    suspend fun checkForUpdates(callback: UpdateCallback) = withContext(Dispatchers.IO) {
        var releaseConnection: HttpURLConnection? = null
        try {
            Log.i(TAG, "Checking for latest SplitOS releases at: $RELEASES_API_URL")
            val url = URL(RELEASES_API_URL)
            val connection = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                setRequestProperty("Accept", "application/vnd.github.v3+json")
                setRequestProperty("User-Agent", "SplitOS-Android-Host/1.0.0")
                connectTimeout = 8000
                readTimeout = 8000
            }

            releaseConnection = connection
            val responseCode = connection.responseCode
            if (responseCode == HttpURLConnection.HTTP_OK) {
                val responseText = connection.inputStream.bufferedReader().use { it.readText() }
                val json = JSONObject(responseText)

                val tagName = json.optString("tag_name", "v1.0.0")
                val releaseName = json.optString("name", "SplitOS Release")
                val releaseNotes = json.optString("body", "")

                var guestOsUrl: String? = null
                var apkUrl: String? = null

                val assets = json.optJSONArray("assets")
                if (assets != null) {
                    for (i in 0 until assets.length()) {
                        val asset = assets.getJSONObject(i)
                        val name = asset.optString("name", "")
                        val downloadUrl = asset.optString("browser_download_url", "")
                        if (name == GUEST_OS_FILE) {
                            guestOsUrl = downloadUrl
                        } else if (name.endsWith(".apk")) {
                            apkUrl = downloadUrl
                        }
                    }
                }

                val info = ReleaseInfo(tagName, releaseName, releaseNotes, guestOsUrl, apkUrl)
                withContext(Dispatchers.Main) {
                    if (guestOsUrl != null || apkUrl != null) {
                        callback.onUpdateAvailable(info)
                    } else {
                        callback.onNoUpdateAvailable()
                    }
                }
            } else {
                withContext(Dispatchers.Main) {
                    callback.onError("GitHub API returned HTTP $responseCode")
                }
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            Log.w(TAG, "Update check failed: ${e.message}")
            withContext(Dispatchers.Main) {
                callback.onError(e.message ?: "Failed to connect to GitHub")
            }
        } finally {
            releaseConnection?.disconnect()
        }
    }

    suspend fun downloadGuestOsImage(downloadUrl: String, callback: UpdateCallback) = withContext(Dispatchers.IO) {
        var connection: HttpURLConnection? = null
        var tempFile: File? = null
        try {
            val url = URL(downloadUrl)
            require(url.protocol == "https") { "Image downloads require HTTPS" }
            val request = (url.openConnection() as HttpURLConnection).apply {
                connectTimeout = 15000
                readTimeout = 30000
            }
            connection = request
            require(request.responseCode == HttpURLConnection.HTTP_OK) {
                "Image download returned HTTP ${request.responseCode}"
            }
            val fileLength = request.contentLengthLong
            require(fileLength <= MAX_IMAGE_BYTES) { "Guest image exceeds RAM limit" }
            val staged = File.createTempFile("guest_os-", ".tmp", context.filesDir)
            tempFile = staged
            var totalBytes = 0L
            request.inputStream.use { input ->
                FileOutputStream(staged).use { output ->
                    val buffer = ByteArray(8192)
                    var bytesRead: Int
                    while (input.read(buffer).also { bytesRead = it } != -1) {
                        currentCoroutineContext().ensureActive()
                        totalBytes += bytesRead
                        require(totalBytes <= MAX_IMAGE_BYTES) { "Guest image exceeds RAM limit" }
                        output.write(buffer, 0, bytesRead)
                        if (fileLength > 0) {
                            withContext(Dispatchers.Main) {
                                callback.onDownloadProgress(((totalBytes * 100) / fileLength).toInt().coerceAtMost(100))
                            }
                        }
                    }
                    output.fd.sync()
                }
            }
            require(fileLength < 0 || totalBytes == fileLength) { "Incomplete image download" }
            validateGuestImage(staged)
            currentCoroutineContext().ensureActive()
            val destFile = File(context.filesDir, GUEST_OS_FILE)
            // Keep the installed image intact if validation or atomic replacement fails.
            Files.move(staged.toPath(), destFile.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
            withContext(Dispatchers.Main) { callback.onUpdateComplete(destFile) }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            Log.e(TAG, "Download error: ${e.message}", e)
            withContext(Dispatchers.Main) { callback.onError(e.message ?: "Download failed") }
        } finally {
            connection?.disconnect()
            tempFile?.delete()
        }
    }
}
