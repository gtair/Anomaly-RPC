# What it is
AnomalyRPC is an Addon that implements discord rich presence in S.T.A.L.K.E.R. Anomaly, with templates and many placeholder values to customize your activity to your needs.

# Installation
Simply download the zip from the latest release and drag the gamedata and bin folders into your root anomaly folder. The script doesn't replace any files so if it warns you about a file conflict, you likely have an actual conflict with another mod. 

# Bug reports
If you want to submit a bug report or need help with a problem on your end, having MCM installed is a requirement. It is essential to enable verbose logging and logging to file. Send me a friend request on discord or contact me on the [official anomaly discord server](https://discord.gg/c4RuJNs) @gtair

# Customization
While the addon will work out of the box and look great at it, you can fully customize the look of the rpc in [this](https://github.com/gtair/Anomaly-RPC/blob/main/gamedata/configs/rpc_config.ltx) configuration file.

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

## advanced customization
To use your own discord app (in order to, for example, change the name of the app or to upload your own assets without having to host them on an external page) you can edit [this](https://github.com/gtair/Anomaly-RPC/blob/5266374ea411da60dc1094ab79a682d84dbed066/main.cpp#L16) variable to your own app id and then compile d3d11.dll yourself
