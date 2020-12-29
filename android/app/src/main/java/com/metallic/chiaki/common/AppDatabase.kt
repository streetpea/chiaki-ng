// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import android.content.Context
import androidx.room.*
import androidx.room.migration.Migration
import androidx.sqlite.db.SupportSQLiteDatabase
import com.metallic.chiaki.lib.Target

@Database(
	version = 2,
	entities = [RegisteredHost::class, ManualHost::class])
@TypeConverters(Converters::class)
abstract class AppDatabase: RoomDatabase()
{
	abstract fun registeredHostDao(): RegisteredHostDao
	abstract fun manualHostDao(): ManualHostDao
	abstract fun importDao(): ImportDao
}

val MIGRATION_1_2 = object : Migration(1, 2)
{
	override fun migrate(database: SupportSQLiteDatabase)
	{
		database.execSQL("ALTER TABLE registered_host ADD target INTEGER NOT NULL DEFAULT 1000")
		database.execSQL("CREATE TABLE `new_registered_host` (`id` INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, `target` INTEGER NOT NULL, `ap_ssid` TEXT, `ap_bssid` TEXT, `ap_key` TEXT, `ap_name` TEXT, `server_mac` INTEGER NOT NULL, `server_nickname` TEXT, `rp_regist_key` BLOB NOT NULL, `rp_key_type` INTEGER NOT NULL, `rp_key` BLOB NOT NULL)");
		database.execSQL("INSERT INTO `new_registered_host` SELECT `id`, `target`, `ap_ssid`, `ap_bssid`, `ap_key`, `ap_name`, `ps4_mac`, `ps4_nickname`, `rp_regist_key`, `rp_key_type`, `rp_key` FROM `registered_host`")
		database.execSQL("DROP TABLE registered_host")
		database.execSQL("ALTER TABLE new_registered_host RENAME TO registered_host")
	}
}

private var database: AppDatabase? = null
fun getDatabase(context: Context): AppDatabase
{
	val currentDb = database
	if(currentDb != null)
		return currentDb
	val db = Room.databaseBuilder(
		context.applicationContext,
		AppDatabase::class.java,
		"chiaki")
		.addMigrations(MIGRATION_1_2)
		.build()
	database = db
	return db
}

private class Converters
{
	@TypeConverter
	fun macFromValue(v: Long) = MacAddress(v)

	@TypeConverter
	fun macToValue(addr: MacAddress) = addr.value

	@TypeConverter
	fun targetFromValue(v: Int) = Target.fromValue(v)

	@TypeConverter
	fun targetToValue(target: Target) = target.value
}