# Scene file components

Generated from the component reflection tables by `zodar::sceneComponentReference()`
(`engine/rendering/SceneFile`); do not edit by hand. Regenerate with
`ZODAR_UPDATE_DOCS=1 ./build/bin/ZodarTests -tc='scene file: reflection*'`.
Every field is optional. Vector ranges apply per component. Scene file format:
`SCENE_FILES.md`.

## `transform`

Local transform relative to the parent (translate * rotate * scale).

| Field | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `position` | [3] | `[0, 0, 0]` |  | Local position (world units) |
| `rotation` | quat [x, y, z, w] | `[0, 0, 0, 1]` |  | Unit quaternion, written [x, y, z, w] |
| `scale` | [3] | `[1, 1, 1]` |  | Local scale |

Also accepted: `rotationDegrees` [3] (Euler XYZ degrees, instead of `rotation`) and a number for a
uniform `scale`.

## `camera`

Camera looking along the entity's -Z axis.

| Field | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `orthographic` | bool | `false` |  | Orthographic instead of perspective |
| `verticalFovDegrees` | number | `60` | 1 .. 179 | Perspective vertical field of view (degrees) |
| `nearPlane` | number | `0.05` | >= 1e-06 | Near plane distance |
| `farPlane` | number | `0` | >= 0 | Far plane distance; 0 = infinite |
| `orthographicHeight` | number | `10` | >= 1e-06 | World units covered vertically (orthographic) |
| `active` | bool | `true` |  | Considered by collectCamera |

## `directionalLight`

Sun light travelling along the entity's -Z axis.

| Field | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `color` | [3] | `[1, 1, 1]` | >= 0 | Linear colour |
| `intensity` | number | `3` | >= 0 | Irradiance multiplier |

## `pointLight`

Point light at the entity's world position.

| Field | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `color` | [3] | `[1, 1, 1]` | >= 0 | Linear colour |
| `intensity` | number | `10` | >= 0 | Radiant intensity (inverse-square) |
| `range` | number | `0` | >= 0 | Cut-off distance; 0 = automatic |

## `spotLight`

Spot light pointing along the entity's -Z axis.

| Field | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `color` | [3] | `[1, 1, 1]` | >= 0 | Linear colour |
| `intensity` | number | `10` | >= 0 | Radiant intensity (inverse-square) |
| `range` | number | `0` | >= 0 | Cut-off distance; 0 = automatic |
| `innerConeDegrees` | number | `20` | 0 .. 89.9 | Full intensity inside (half angle, degrees) |
| `outerConeDegrees` | number | `31.5` | 0 .. 89.9 | Zero outside (half angle, degrees) |

## `environment`

Ambient light, clear colour and IBL intensity.

| Field | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `clearColor` | [4] | `[0, 0, 0, 1]` | >= 0 | Linear HDR clear colour |
| `ambientColor` | [3] | `[1, 1, 1]` | >= 0 | Constant ambient (used without an IBL environment) |
| `ambientIntensity` | number | `0.03` | >= 0 | Ambient multiplier |
| `environmentIntensity` | number | `1` | >= 0 | IBL multiplier |
| `drawBackground` | bool | `true` |  | Draw the IBL sky where no geometry was drawn |

## `fog`

Distance + exponential height fog (presence enables it).

| Field | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `color` | [3] | `[0.5, 0.6, 0.7]` | >= 0 | Linear colour |
| `density` | number | `0.01` | >= 0 | Extinction per world unit at baseHeight |
| `heightFalloff` | number | `0` | >= 0 | 0 = uniform |
| `baseHeight` | number | `0` |  | Reference height |
| `startDistance` | number | `0` | >= 0 | No fog closer than this |
| `maxOpacity` | number | `1` | 0 .. 1 | Opacity clamp |
