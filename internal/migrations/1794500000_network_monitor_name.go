package migrations

import (
	"ganigeorgiev.com/pocketbase/pocketbase/core"
	m "ganigeorgiev.com/pocketbase/pocketbase/migrations"
)

func init() {
	m.Register(func(app core.App) error {
		collection, err := app.FindCollectionByNameOrId("network_monitors")
		if err != nil {
			return err
		}
		// Optional display name. Existing records keep an empty name and fall
		// back to showing their target.
		collection.Fields.Add(&core.TextField{
			Id:   "nm_name",
			Name: "name",
			Max:  100,
		})
		return app.Save(collection)
	}, func(app core.App) error {
		collection, err := app.FindCollectionByNameOrId("network_monitors")
		if err != nil {
			return err
		}
		collection.Fields.RemoveById("nm_name")
		return app.Save(collection)
	})
}
