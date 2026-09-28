# Final production-engine hardware scenarios

All three scenarios use the same native ELF, the source-identical shipped engine with
deep attribution off and no cache/raster/oracle switches. Every capture has 960 aligned
frame, attribution and traffic rows, with exports deferred past sampling.

| Window | Median work | p95 | >20 ms | Rolling FPS |
|---|---:|---:|---:|---:|
| Stationary night entry, frames 240–479 | 21.232 ms | 22.538 ms | 240/240 | 25.000 |
| Full throttle, first 120 frames after 5 m | 17.393 ms | 20.705 ms | 8/120 | 42.297 |
| Seated/no throttle, moving AI, frames 240–479 | 19.709 ms | 20.993 ms | 84/240 | 36.545 |

The driving window is frames **232–351**, beginning at
X 0.000000, Z -68.722527, speed 11.374483.
The earlier pre-fix five-metre capture had median work 18.722 ms and 29/120 over budget;
the final build has 17.393 ms and 8/120. This is a regression gate at the same geographic
milestone, not a same-ELF subtraction: FPS changes simulation step size and trajectories.
Rolling FPS is a history window and does not equal the reciprocal of median active work.

In stationary and driving scenarios both other cars have exactly zero speed and no
position change across all 960 rows. In the traffic scenario Ravager still has exactly
zero speed, X 0 and unchanged Z -74.303497. Both AI cars actually move:
Pica max speed 19.960480, Strix 23.225876;
their Z position ranges are 49.570999 and 52.260645.
These scratch routes exercise moving cars and cached local bounds under changing
transforms; they do not describe a map-wide traffic minimum. Their different changing
view cannot be compared directly to the fixed parked view as an optimization delta.

Exact first-entry frame 180 still costs **163.235 ms**, including
148.902 ms render submission and 13.241 ms update.
The font atlas/icon sheet remain lazy loads in the entry log; no first-entry hitch fix
is claimed. The final stationary view still has 240/240 warm frames above 20 ms, with
all effects retained and 46,096 submitted primitives. Stable garage 50 FPS remains open.

ELF SHA-256: `95cf89cc14fbc7325c0653831d7a6fc73b25a20dc6202c5ff64b54c069b48dda`.

[Full acceptance report](README.md) | [Stationary capture](production-stationary/) | [Driving capture](production-drive/) | [Traffic capture](production-traffic/)
