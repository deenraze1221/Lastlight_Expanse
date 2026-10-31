# Economy pacing model

Reproduce: node scripts/economy-report.js. Model assumes 65% accuracy, the standard 55/20/20/5 enemy mix, all cash collected, and continuous ammo availability at the terminal. It excludes travel time and damage taken; this is a resource model, not a playtest. Repair cost is amortized at $0.50 per shot (5 condition per 100 shots, $500 per 50 condition). Prices, magazines and damage follow the current weapon configuration.

Average kill payout: $28.5.

| Weapon | Shots/kill incl. misses | Ammo + repair/kill | Net cash/kill |
| --- | --- | --- | --- |
| Pistol | 8.9 | $13.4 | $15.1 |
| Helsing | 11.6 | $17.4 | $11.1 |
| Tikhar | 3.2 | $4.7 | $23.8 |
| Shambler | 6.2 | $9.3 | $19.2 |
| Shotgun | 4.8 | $7.2 | $21.3 |
| Kalash | 7.6 | $11.4 | $17.1 |
| VSV | 5.7 | $8.5 | $20.0 |
| Scorpion | 8.9 | $13.4 | $15.1 |
| Vector | 10.2 | $15.3 | $13.2 |
| G36 | 10.2 | $15.3 | $13.2 |

Wave-clear bonus is $75 + $25 × wave number; each fifth wave pays $500 instead. Shared missions roll with15% chance from wave3 and pay300–600 per player. Early-wave zombies/runners average $26 per kill. First-wave expected kill income plus clear bonus is15 × $26 + $100 ≈ $490, before ammunition/repair and healing costs. New ammunition packs contain50 rounds for $50. The first two waves have no missions. No automatic ammo or health gifts are introduced.
