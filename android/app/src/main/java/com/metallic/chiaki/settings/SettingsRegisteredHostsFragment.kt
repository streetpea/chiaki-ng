// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import android.app.ActivityOptions
import android.content.Intent
import android.content.res.Resources
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatDialogFragment
import androidx.lifecycle.Observer
import androidx.lifecycle.ViewModelProvider
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.metallic.chiaki.R
import com.metallic.chiaki.common.ext.putRevealExtra
import com.metallic.chiaki.common.ext.viewModelFactory
import com.metallic.chiaki.common.getDatabase
import com.metallic.chiaki.regist.RegistActivity
import kotlinx.android.synthetic.main.fragment_settings_registered_hosts.*

class SettingsRegisteredHostsFragment: AppCompatDialogFragment(), TitleFragment
{
	private lateinit var viewModel: SettingsRegisteredHostsViewModel

	override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View =
		inflater.inflate(R.layout.fragment_settings_registered_hosts, container, false)

	override fun onViewCreated(view: View, savedInstanceState: Bundle?)
	{
		viewModel = ViewModelProvider(this, viewModelFactory { SettingsRegisteredHostsViewModel(getDatabase(context!!)) })
			.get(SettingsRegisteredHostsViewModel::class.java)

		val adapter = SettingsRegisteredHostsAdapter()
		hostsRecyclerView.layoutManager = LinearLayoutManager(context)
		hostsRecyclerView.adapter = adapter
		val itemTouchSwipeCallback = object : ItemTouchSwipeCallback(context!!)
		{
			override fun onSwiped(viewHolder: RecyclerView.ViewHolder, direction: Int)
			{
				val pos = viewHolder.adapterPosition
				val host = viewModel.registeredHosts.value?.getOrNull(pos) ?: return
				MaterialAlertDialogBuilder(viewHolder.itemView.context)
					.setMessage(getString(R.string.alert_message_delete_registered_host, host.serverNickname, host.serverMac.toString()))
					.setPositiveButton(R.string.action_delete) { _, _ ->
						viewModel.deleteHost(host)
					}
					.setNegativeButton(R.string.action_keep) { _, _ ->
						adapter.notifyItemChanged(pos) // to reset the swipe
					}
					.create()
					.show()
			}
		}
		ItemTouchHelper(itemTouchSwipeCallback).attachToRecyclerView(hostsRecyclerView)
		viewModel.registeredHosts.observe(this, Observer {
			adapter.hosts = it
			emptyInfoGroup.visibility = if(it.isEmpty()) View.VISIBLE else View.GONE
		})

		floatingActionButton.setOnClickListener {
			Intent(context, RegistActivity::class.java).also {
				it.putRevealExtra(floatingActionButton, rootLayout)
				startActivity(it, ActivityOptions.makeSceneTransitionAnimation(activity).toBundle())
			}
		}
	}

	override fun getTitle(resources: Resources): String = resources.getString(R.string.preferences_registered_hosts_title)
}