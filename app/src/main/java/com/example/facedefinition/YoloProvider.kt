package com.example.facedefinition

import android.content.res.AssetManager
import android.view.Surface
import com.example.facedefinition.MainActivity

class YoloProvider {

    init {
        System.loadLibrary("yolonccn")
    }

    external fun loadModel(mgr: AssetManager?): Boolean
    external fun openCamera(facing: Int): Boolean
    external fun closeCamera(): Boolean
    external fun setOutputWindow(surface: Surface?): Boolean
    external fun injectObjectReference(mainActivity: MainActivity): Boolean
}