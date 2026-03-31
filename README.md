<div align="center">
   <img width="170" height="170" src="/assets/studiologo.png" alt="Logo">
</div>
<div align="center">
  <h1><b>StudioRMDL</b></h1>
  <p><i>A model compiler for Apex Legends Season 3 Models (RMDL v54 subversion 10) for <a href="https://discord.gg/wVsudEruAx">R5Valkyrie</a>.</i></p>
</div>

> [!WARNING]
> **Notice:** StudioRMDL is not complete, issues and other bugs may arise. Please open an issue and be descriptive with your issue and how to recreate it

## Features
- **RUI Meshes:** Allows for RUI Meshes to be compiled into models
- **Complete Compilation:** Compiles straight to RMDL, VG and PHY (if applicable)
- **Animation Conversion:** **StudioRMDL** can convert animations from the SMD into rseq and rig
- **VMT/SKNP TEXTURES:** **StudioRMDL** can null the texture slots so it falls back to the old valve VMT system
- **RUI DUMPING:** Included **Python** script that can dump RUI Meshes from RMDL's for use

## Usage
### Basic usage
```
studiormdl.exe -game <gamedir> mymodel.qc
```
***
**Note :** `-game` is not required and can be used standalone
```
studiormdl.exe mymodel.qc
```
***
### Supported QC commands

All standard studiomdl QC commands work normally.  The following are specific to this build or have Apex-specific behaviour:

### `$ruimeshfile` *(Apex-specific)*
```
$ruimeshfile "mymodel.rui"
```
Embeds a `.rui` RUI-mesh file into the RMDL.  Only needed for models that have Respawn UI panels attached to bones (e.g. weapon stat displays, ammo counters).  Most models don't need this — omit it for props and regular weapons.

### `$renamematerial` rather than `$cdmaterials`
`$cdmaterials` work fine if you have a single material or multiple materials have that you want to be in the same material dir, however using if you need to have them in different directories, use `$renamematerials` as shown below

```$renamematerial "mc_mtl_p7_zm_vending_pap_detail" "models\zombies\vending_pap_detail_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_frame_right" "models\zombies\vending_pap_frame_right_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_frame_left" "models\zombies\vending_pap_frame_left_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_side_inside" "models\zombies\vending_pap_side_inside_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_top_main" "models\zombies\vending_pap_top_main_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_top" "models\zombies\vending_pap_top_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_front_side" "models\zombies\vending_pap_front_side_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_bottom_back" "models\zombies\vending_pap_bottom_back_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_packapunch" "models\zombies\vending_packapunch_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_logo" "models\zombies\vending_pap_logo_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_packapunch_sign" "models\zombies\vending_packapunch_sign_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_leather" "models\zombies\vending_pap_leather_rgdp"
$renamematerial "mc_mtl_p7_zm_vending_pap_roller" "models\zombies\vending_pap_roller_rgdp" 
```

### Dumping RUI Meshes
Using the `rmdl_dump_rui.py` on a existing RMDL with an RUI mesh on it, like the ptpov_97 or ptpov_vinson, you can get the .rui out of it
```
rmdl_dump_rui.py ptpov_97.rmdl ptpov_97.rui
```

### Command Line Args
There are a few command line args that change the output of StudioRMDL, notably
- `-vmtext` - sets all the texture slots to 0x0 so they use VMTs instead of Apex Materials
- `-convertanims` converts the mdl/smd anims to `rseq` and `rrig`, you need to use it with -sp (sequence path) and -rp (rig path) as shown below as an example
```
studiormdl.exe my_model.qc -convertanims -rp animrig/<usage of model>/<model name> -sp animseq/<usage of model>/<model name> 
```
R99 as an example
```
studiormdl.exe ptpov_97.qc -convertanims -rp animrig/weapons/r97 -sp animseq/weapons/r97 
```


## Credits
StudioRMDL wouldn't be possible without these people and their work:
- **[rmdlconv](https://github.com/r-ex/rmdlconv) by rexx** – model conversion tool for Source & Respawn MDL formats (used for RMDL export logic)
- **[R5-AnimConv](https://github.com/someoneatemylastsliceofpizza/R5-AnimConv) by someoneatemylastsliceofpizza** – animation converter for mdl animations to rseq/rrig, referenced for animation conversion.  
- **[Resource Templates](https://github.com/IJARika/resource_model_templates) by Rika** – templates for models, animations, and rigs

<div align="center">
  <h3><b>My Other Projects</b></h3>
</div>
<div align="center">

<table>
<tr>

<td align="center" width="170">
  <a href="https://github.com/WateryContinent/rmdledit">
    <img src="/assets/editlogo.png" width="80" height="80"><br>
    <b>RMDLEdit</b><br>
  </a>
</td>

<td align="center" width="170">
  <a href="https://github.com/WateryContinent/bonejig">
    <img src="/assets/jiglogo.png" width="80" height="80"><br>
    <b>BoneJig</b><br>
  </a>
</td>

<td align="center" width="170">
  <a href="https://github.com/WateryContinent/qcforge">
    <img src="/assets/qcgenlogo.png" width="80" height="80"><br>
    <b>QCForge</b><br>
  </a>
</td>

</tr>
</table>

<div align="center">
  <h3><b><a href="https://discord.gg/s66qvh4brh">My Discord</a></b></h3>
</div>

</div>
