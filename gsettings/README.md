# gsettings backend

This directory contains the Wayfire configuration backend that syncs `wf-config`
options with `org.wayfire.*` GSettings schemas.

## components

- `gsettings-config-backend.cpp`: backend wiring between Wayfire config sections
  and GSettings objects.
- `gsettings-glib-source.*`: bridge that dispatches a GLib main context from the
  Wayland event loop.
- `gsettings-mapping.*`: helpers for key-name and type-signature mapping.
- `gsettings-variant.*`: option <-> `GVariant` conversion helpers.
- `gsettings-seed.*`: seeding logic for relocatable defaults (for example output scale).
- `tools/gsettings-schema-gen.cpp`: schema generator from Wayfire metadata XML.

## reference credits

This alternative approach was developed with
[wf-gsettings](https://codeberg.org/valpackett/wf-gsettings) as an earlier
reference implementation.
