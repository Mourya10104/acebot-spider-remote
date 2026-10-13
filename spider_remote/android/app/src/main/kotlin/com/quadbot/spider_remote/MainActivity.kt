package com.quadbot.spider_remote

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity

/**
 * The robot's WiFi has no internet. Android notices that and may quietly send
 * our traffic over mobile data instead, so requests to 192.168.4.1 would fail.
 *
 * Fix: watch for a WiFi network and "bind" this whole app process to it.
 * After that, every socket the app opens (including Flutter's) uses WiFi.
 */
class MainActivity : FlutterActivity() {

    private lateinit var cm: ConnectivityManager

    private val wifiCallback = object : ConnectivityManager.NetworkCallback() {
        // A WiFi network appeared (or we joined one): route our traffic over it.
        override fun onAvailable(network: Network) {
            cm.bindProcessToNetwork(network)
        }

        // WiFi went away: release the binding so the app behaves normally.
        override fun onLost(network: Network) {
            cm.bindProcessToNetwork(null)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

        // Only match WiFi networks. We deliberately do NOT require the
        // INTERNET capability, because the robot's WiFi doesn't have it.
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .build()
        cm.registerNetworkCallback(request, wifiCallback)
    }

    override fun onDestroy() {
        try {
            cm.unregisterNetworkCallback(wifiCallback)
            cm.bindProcessToNetwork(null)
        } catch (_: Exception) {
        }
        super.onDestroy()
    }
}
