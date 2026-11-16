package com.splitos.updater

import java.io.File
import java.io.RandomAccessFile
import org.junit.Assert.assertThrows
import org.junit.Test

class UpdateManagerTest {
    @Test fun validatesImageBeforeReplacement() {
        val image = File.createTempFile("guest-image", ".bin")
        try {
            assertThrows(IllegalArgumentException::class.java) { UpdateManager.validateGuestImage(image) }
            image.writeBytes(ByteArray(32))
            assertThrows(IllegalArgumentException::class.java) { UpdateManager.validateGuestImage(image) }
            image.writeBytes("SPLITOS\u0000".toByteArray() + ByteArray(24))
            UpdateManager.validateGuestImage(image)
            RandomAccessFile(image, "rw").use { it.setLength(16 * 1024 * 1024L + 1) }
            assertThrows(IllegalArgumentException::class.java) { UpdateManager.validateGuestImage(image) }
        } finally {
            image.delete()
        }
    }
}
