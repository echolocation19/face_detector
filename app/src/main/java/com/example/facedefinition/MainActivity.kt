package com.example.facedefinition

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.PixelFormat
import android.os.Bundle
import android.view.SurfaceHolder
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.example.facedefinition.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private val binding: ActivityMainBinding by lazy { ActivityMainBinding.inflate(layoutInflater) }
    private val yolo = YoloProvider()
    private var facing = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(binding.root)
        with(binding.cameraView) {
            holder.setFormat(PixelFormat.RGBA_8888)
            holder.addCallback(this@MainActivity)
        }
        yolo.injectObjectReference(this)
        binding.buttonSwitchCamera.setOnClickListener {
            val facing = 1 - facing
            yolo.closeCamera()
            yolo.openCamera(facing)
            this.facing = facing
        }
        yolo.loadModel(assets)
    }

    override fun surfaceCreated(holder: SurfaceHolder) = Unit

    override fun surfaceDestroyed(holder: SurfaceHolder) = Unit

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        yolo.setOutputWindow(holder.surface)
    }

    override fun onResume() {
        super.onResume()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_DENIED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA), CAMERA_REQUEST_CODE)
        }
        yolo.openCamera(facing)
    }

    override fun onPause() {
        super.onPause()
        yolo.closeCamera()
    }

    private companion object {

        const val CAMERA_REQUEST_CODE = 1
    }
}