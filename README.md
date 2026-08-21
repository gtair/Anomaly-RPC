# Installation
Simply download the zip from the latest release and drag the gamedata folder into your game root Then disable the basic anomaly rpc in `settings > Others > Discord Status`

## Compatability
This mod should be compatible with virtually every other mod for this game. If any conflicts arise, please let me know.

## Use in Modpacks
Same as with most other mods, as long as my license file that is shipped in the mod is preserved I'd love to see this in modpacks

# Bug reports and contact
If you want to submit a bug report or need help with a problem on your end, having MCM installed is a requirement. It is essential to enable verbose logging. Write a comment on the [moddb page](https://www.moddb.com/mods/stalker-anomaly/addons/anomalyrpc/) of the addon. Additionally you can find me on discord (including the official anomaly discord server) under the handle @gtair

# Customization
While the addon will work out of the box and look great at it, you can fully customize the look of the rpc in [this](https://github.com/gtair/Anomaly-RPC/blob/main/gamedata/configs/rpc_config.ltx) configuration file;

## Keys you can change
<img width="452" height="184" alt="Activity_Boxes" src="https://github.com/user-attachments/assets/3bf78e98-0170-43e3-91ea-c4a2d0183f69" />

* :red_circle: `details`
* :green_circle: `state`
* :purple_circle: `large_image_key`, `large_image_text`
* :brown_circle: `small_image_key`, `small_image_text`
> [!IMPORTANT]
> both `image_key`s have to either be full https links or assets of your discord app
* :yellow_circle: `party_size`, `party_max`
* :large_blue_circle: `button_1/2_label`, `button_1/2_url`

## placeholders
| Key | Description | Example Output |
| :--- | :---: | ---: |
| `{level}` | Translated map name | Cordon |
| `{level_raw}` | Raw map name | l01_escape |
| `{faction}` | Translated faction name | Mercenary |
| `{faction_raw}` | Raw faction name | killer |
| `{active_task}` | The currently active task | Turn off the Brain Scorcher |
| `{money}` | Player cash in Rubles | 69420 |
| `{health}` | Player health percentage | 65 |
| `{squad_size}` | Total size of your active squad (including you) | 3 |
| | |
| `{rank}` | Translated rank name | novice |
| `{rank_raw}` | Raw numeric rank value | 393 |
| `{reputation}` | Translated reputation name | Neutral |
| `{reputation_raw}` | Raw numeric reputation value | 495 |
| | |
| `{mutants_killed}` | Total mutants killed | 5 |
| `{stalkers_killed}` | Total stalkers killed | 36 |
| `{pdas_delivered}` | PDAs delivered to traders | 2 |
| `{boxes_smashed}` | Loot boxes/crates smashed | 12 |
| `{level_changes}` | Number of times you've changed levels/maps | 23 |
| `{tasks_completed}` | Tasks/quests completed | 16 |
| `{artifacts_detected}` | Artifacts detected in anomaly fields | 4 |
| `{enemy_forfeits}` | Enemies who surrendered to you | 0 |
| `{helicopters_downed}` | Helicopters shot down | 0 |
| `{stashes_found}` | Stashes found | 7 |
| `{wounded_helped}` | Wounded NPCs helped/healed | 1 |
| `{field_dressings}` | Field dressings/bandages applied | 7 |
