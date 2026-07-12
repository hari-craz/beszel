package migrations

import (
	"github.com/pocketbase/pocketbase/core"
	m "github.com/pocketbase/pocketbase/migrations"
)

func init() {
	m.Register(func(app core.App) error {
		jsonData := `[
	{
		"id": "recoverymodules",
		"name": "recovery_modules",
		"type": "base",
		"system": false,
		"listRule": "@request.auth.id != \"\"",
		"viewRule": "@request.auth.id != \"\"",
		"createRule": "@request.auth.id != \"\" && @request.auth.role = \"admin\"",
		"updateRule": "@request.auth.id != \"\" && @request.auth.role = \"admin\"",
		"deleteRule": "@request.auth.id != \"\" && @request.auth.role = \"admin\"",
		"fields": [
			{
				"id": "text3208210256",
				"name": "id",
				"type": "text",
				"required": true,
				"primaryKey": true,
				"system": true
			},
			{
				"id": "modulename001",
				"name": "name",
				"type": "text",
				"required": true
			},
			{
				"id": "modulemac0001",
				"name": "mac_address",
				"type": "text",
				"required": true
			},
			{
				"id": "moduleip00001",
				"name": "ip_address",
				"type": "text",
				"required": false
			},
			{
				"id": "modulegwip0001",
				"name": "gateway_ip",
				"type": "text",
				"required": false
			},
			{
				"id": "modulegwname01",
				"name": "gateway_name",
				"type": "text",
				"required": false
			},
			{
				"id": "modulemaxch01",
				"name": "max_channels",
				"type": "number",
				"required": true,
				"onlyInt": true
			},
			{
				"id": "modulever0001",
				"name": "firmware_version",
				"type": "text",
				"required": true
			},
			{
				"id": "modulestat001",
				"name": "status",
				"type": "text",
				"required": true
			},
			{
				"id": "modulerev0001",
				"name": "config_revision",
				"type": "number",
				"required": true,
				"onlyInt": true
			},
			{
				"id": "modulehash001",
				"name": "config_hash",
				"type": "text",
				"required": false
			},
			{
				"id": "moduletemp0001",
				"name": "temperature",
				"type": "number",
				"required": false
			},
			{
				"id": "moduletempwarn",
				"name": "temp_threshold_warning",
				"type": "number",
				"required": false
			},
			{
				"id": "moduletempcrit",
				"name": "temp_threshold_critical",
				"type": "number",
				"required": false
			},
			{
				"id": "autodate2990389176",
				"name": "created",
				"type": "autodate",
				"onCreate": true,
				"onUpdate": false
			},
			{
				"id": "autodate3332085495",
				"name": "updated",
				"type": "autodate",
				"onCreate": true,
				"onUpdate": true
			}
		]
	},
	{
		"id": "recoverychannel",
		"name": "recovery_channels",
		"type": "base",
		"system": false,
		"listRule": "@request.auth.id != \"\"",
		"viewRule": "@request.auth.id != \"\"",
		"createRule": "@request.auth.id != \"\" && @request.auth.role != \"readonly\"",
		"updateRule": "@request.auth.id != \"\" && @request.auth.role != \"readonly\"",
		"deleteRule": "@request.auth.id != \"\" && @request.auth.role != \"readonly\"",
		"fields": [
			{
				"id": "text3208210256",
				"name": "id",
				"type": "text",
				"required": true,
				"primaryKey": true,
				"system": true
			},
			{
				"id": "chanmodule001",
				"name": "module",
				"type": "relation",
				"required": false,
				"collectionId": "recoverymodules",
				"cascadeDelete": true,
				"maxSelect": 1
			},
			{
				"id": "channum0000001",
				"name": "channel_number",
				"type": "number",
				"required": false,
				"onlyInt": true
			},
			{
				"id": "chansystem001",
				"name": "system",
				"type": "relation",
				"required": true,
				"collectionId": "2hz5ncl8tizk5nx",
				"cascadeDelete": true,
				"maxSelect": 1
			},
			{
				"id": "chanhostip001",
				"name": "host_ip",
				"type": "text",
				"required": true
			},
			{
				"id": "chanports0001",
				"name": "probe_ports",
				"type": "json",
				"required": true
			},
			{
				"id": "chanthresh001",
				"name": "failure_threshold",
				"type": "number",
				"required": true,
				"onlyInt": true
			},
			{
				"id": "chanbootgrace",
				"name": "boot_grace_seconds",
				"type": "number",
				"required": true,
				"onlyInt": true
			},
			{
				"id": "chanmaint0001",
				"name": "maintenance",
				"type": "bool",
				"required": false
			},
			{
				"id": "chanwolenable01",
				"name": "wol_enabled",
				"type": "bool",
				"required": false
			},
			{
				"id": "chanautowol001",
				"name": "auto_wol",
				"type": "bool",
				"required": false
			},
			{
				"id": "chanmacaddr001",
				"name": "mac_address",
				"type": "text",
				"required": false
			},
			{
				"id": "chanbcaddr0001",
				"name": "broadcast_address",
				"type": "text",
				"required": false
			},
			{
				"id": "chanwolport001",
				"name": "wol_port",
				"type": "number",
				"required": false,
				"onlyInt": true
			},
			{
				"id": "autodate2990389176",
				"name": "created",
				"type": "autodate",
				"onCreate": true,
				"onUpdate": false
			},
			{
				"id": "autodate3332085495",
				"name": "updated",
				"type": "autodate",
				"onCreate": true,
				"onUpdate": true
			}
		]
	},
	{
		"id": "recoveryevents",
		"name": "recovery_events",
		"type": "base",
		"system": false,
		"listRule": "@request.auth.id != \"\"",
		"viewRule": "@request.auth.id != \"\"",
		"createRule": null,
		"updateRule": null,
		"deleteRule": null,
		"fields": [
			{
				"id": "text3208210256",
				"name": "id",
				"type": "text",
				"required": true,
				"primaryKey": true,
				"system": true
			},
			{
				"id": "eventsystem01",
				"name": "system",
				"type": "relation",
				"required": true,
				"collectionId": "2hz5ncl8tizk5nx",
				"cascadeDelete": true,
				"maxSelect": 1
			},
			{
				"id": "eventmodule01",
				"name": "module",
				"type": "relation",
				"required": false,
				"collectionId": "recoverymodules",
				"cascadeDelete": true,
				"maxSelect": 1
			},
			{
				"id": "eventchannel0",
				"name": "channel",
				"type": "number",
				"required": false,
				"onlyInt": true
			},
			{
				"id": "eventtype0001",
				"name": "event",
				"type": "text",
				"required": true
			},
			{
				"id": "eventtime0001",
				"name": "timestamp",
				"type": "date",
				"required": true
			},
			{
				"id": "eventmeta0001",
				"name": "metadata",
				"type": "json",
				"required": false
			},
			{
				"id": "autodate2990389176",
				"name": "created",
				"type": "autodate",
				"onCreate": true,
				"onUpdate": false
			},
			{
				"id": "autodate3332085495",
				"name": "updated",
				"type": "autodate",
				"onCreate": true,
				"onUpdate": true
			}
		]
	}
]`
		return app.ImportCollectionsByMarshaledJSON([]byte(jsonData), false)
	}, func(app core.App) error {
		// optional: remove collections on rollback
		return nil
	})
}
