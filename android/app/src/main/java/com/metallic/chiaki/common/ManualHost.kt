// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import androidx.room.*
import androidx.room.ForeignKey.Companion.SET_NULL
import io.reactivex.Completable
import io.reactivex.Flowable
import io.reactivex.Single

@Entity(tableName = "manual_host",
	foreignKeys = [
	ForeignKey(
		entity = RegisteredHost::class,
		parentColumns = ["id"],
		childColumns = ["registered_host"],
		onDelete = SET_NULL
	)
])
data class ManualHost(
	@PrimaryKey(autoGenerate = true) val id: Long = 0,
	val host: String,
	@ColumnInfo(name = "registered_host") val registeredHost: Long?
)

data class ManualHostAndRegisteredHost(
	@Embedded(prefix = "manual_host_") val manualHost: ManualHost,
	@Embedded val registeredHost: RegisteredHost?
)

@Dao
interface ManualHostDao
{
	@Query("SELECT * FROM manual_host WHERE id = :id")
	fun getById(id: Long): Single<ManualHost>

	@Query("""SELECT
			manual_host.id as manual_host_id,
			manual_host.host as manual_host_host,
			manual_host.registered_host as manual_host_registered_host,
			registered_host.*
		FROM manual_host LEFT OUTER JOIN registered_host ON manual_host.registered_host = registered_host.id WHERE manual_host.id = :id""")
	fun getByIdWithRegisteredHost(id: Long): Single<ManualHostAndRegisteredHost>

	@Query("SELECT * FROM manual_host")
	fun getAll(): Flowable<List<ManualHost>>

	@Query("UPDATE manual_host SET registered_host = :registeredHostId WHERE id = :manualHostId")
	fun assignRegisteredHost(manualHostId: Long, registeredHostId: Long?): Completable

	@Insert
	fun insert(host: ManualHost): Completable

	@Delete
	fun delete(host: ManualHost): Completable

	@Update
	fun update(host: ManualHost): Completable
}