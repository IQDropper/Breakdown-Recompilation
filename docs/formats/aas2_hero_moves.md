# Derrick's moves from the hero script (P4.6, first table)

Read straight out of `bhb_mo_hero` with `tools/aas2_dis.py --moves` (no game run). Format guide: `aas2.md`.

**How to read the numbers.** Damage is written in the script as `N x power / 10`. Power is character variable 43
and is normally 10, so **"8P" = 8 damage** at normal power (set by `SUB POWERUP` to 6..30 depending on the move and
power-up mode). A plain number (no P) does not scale. "f22-28" = the hit can land from animation frame 22 to 28.
When the enemy takes the hit, its script divides by its defence: `life -= damage x 10 / defence` (variable 44).

**Buttons.** The script calls the two attack buttons by key-manager bits: **Attack 1 = 0x800**, **Attack 2 = 0x200**
(which pad buttons these are is still open, P4.6a). "Entered from" lists the states or subroutines whose code
changes into this move, with the last button check seen before it (a hint, not a proof).

## The main moves in plain words

| Input | Move (state) | Animation | Damage | Live frames |
|---|---|---|---|---|
| Attack 1 | SudeTachiAttack1 | right straight (`gym_HE_FT_NN_BD_Rstr_NT`) | 8P | 22-28 |
| Attack 1, 1 | SudeTachiAttack1Attack1 | right thrust kick (`RtukiK`) | 10P | 28-34 |
| Attack 1, fast 1 | SudeTachiAttack1Attackfast1 | uppercut (`up_SW`) | 10P | 27-34 |
| Attack 1, fast 1, 2 | ...Attackfast1Attack2 | (placeholder anim) | 18P | 34-44 |
| Attack 2 | SudeTachiAttack2 | left jab (`Ljab`) | 4P | 18-24 |
| Attack 2, 2 | SudeTachiAttack2Attack2 | left jab | 4P | 18-24 |
| Attack 2, 2, 1 | SudeTachiAttack2Attack2Attack1 | great right straight (`GreatRstr`) | 20P | 24-42 |
| Attack 2, 2, 2 | SudeTachiAttack2Attack2Attack2 | anim id 1263 (not in the packs) | 10P | 60-65 |
| Attack 2, 2, slow 1 | ...Attack2Attackslow1 | right straight | 8P | 22-28 |
| Attack 2, 1 | SudeTachiAttack2Attack1 | right straight | 8P | 22-28 |
| Attack 2, 1, 1 | SudeTachiAttack2Attack1Attack1 | right thrust kick | 18P | 28-34 |
| Attack 2, 1, 2 | SudeTachiAttack2Attack1Attack2 | left uppercut (`upL`) | 12P / 15.6P / 16.8P / 24P | 38-44 |
| Attack 2, 1, 2, then hold 2 | ..._start then ...Attack2Attack1Attack2Attack1 | power charge (`genkidama`) then great right straight | **40P** | 24-42 |
| Attack 1 + 2 together | SudeTachiAttack1andAttack2 | lariat (`bomber`) | 6P | 8-38 (long) |
| Hold 1 (power) | SudeTachiAttack2PW | power charge (`genkidama`) | no hit window here | - |
| Forward + 1 | SudeTachiFMove1Attack1 | jumping uppercut | 25P | 32-44 / 32-52 |
| Forward + 2 | SudeTachiFMove1Attack2 | rising kick (`keriageL`) | **48P** | 48-56 |
| Back + 1 | SudeTachiBMove1Attack1 | right thrust kick | 10P / 12P / 13P | 28-34 |
| Back + 2 | SudeTachiBMove1Attack2 | left high kick | 16P | 27-34 |
| Left + 1 | SudeTachiLMove1Attack1 | uppercut | 12P | 22-34 |
| Left + 2 | SudeTachiLMove1Attack2 | left hook | 9P | 28-34 |
| Right + 1 | SudeTachiRMove1Attack1 | right jumping hook (`RHookJ`) | 8P | 19-28 |
| Right + 2, then 2 x5 | SudeTachiRMove1Attack2 ... (6 hits) | left backfist (`UrakenL`) | 8P, then 2P x4, last 12P | 26-32 (last 26-36) |
| combo 1 | SudeTachicombo1_Attack1 / _Attack2 | boomerang hook / left hook | 14P / 14P | 26-32 / 28-34 |
| combo 2 | SudeTachicombo2_Attack1 / _Attack2 | jumping uppercut / chop | 18P / 12P | 32-44 / 20-28 |
| Crouch + 1 | SudeSyagamiAttack1 | crouch punch (`shagaP2`) | 2P | 20-29 |
| Crouch + 2 | SudeSyagamiAttack2 | crouch punch (`shagaP`) | 2P | 13-20 |
| Crouch, moving/turning + 1 or 2 | SudeSyagami{F,B,L,R}Move / Rotate | crouch punch | 8 (flat) | 13-17 |
| In the air + 1 | KucyuuAttack1 | rider kick | 17P, charged: 22.1P, 27.2P, 37.4P, 51P, 68P (and flat 153 / 204) | 29-54 |
| In the air + 2 | KucyuuAttack2 | anim id 5814 (not in the packs) | 12P | 46-55 |
| Enemy on the floor | DownAttack1 | stomp kick (`downk1`) | 10P | 20-36 |
| From guard + 2 | GuardAttack1 | (placeholder anim) | 10P / 20P / 30P (or flat 5) by guard level | 14-32 |
| From guard + 1 | GuardAttack2_st -> GuardAttack2 | great right straight | 5 / 10P | 24-42 |
| Sliding | Sliding | - | 8P | 20-50 |
| Sliding + 2 / + 1 | SlidingAttack1 / SlidingAttack2 | - / sliding uppercut | 18P / 25P | 14-32 / 32-52 |
| Handgun out + 2 | HandGun_Attack2, then 1 | left hook, then left backfist | 7P, 10P | 30-36, 26-32 |
| Handgun out, 1 | HandGun_Attack1 | (placeholder anim) | 10P..40P (by charge step) | 24-34 |
| Handgun out, back + 2 | HandGun_BMoveAttack2 | (placeholder anim) | 15P..45P | 36-46 |
| Rocket launcher out + 2 | Rocket_Attack1 | left high kick | 13P | 26-34 |

"Placeholder anim" = the state first sets a test animation (`gym_testyarare*`) and the real one comes from a
`Motion_*` table later; the tool only follows constants, so those rows still need a closer read.

**Taking damage (Derrick):** 22 places do `Life_AddFrom(-Hit_Damage x 10 / defence)`; 31 places set life to 1
(`Life_AddFrom(-(Life - 1))`, "survive with 1"); falls use `Life_AddFrom((x + 70) / 80 x max life)`; 9 places kill
outright (`-10000`).

## Full table (every attack-like state of Derrick, generated)

| # | State | Animation(s) | Damage, live frames | Entered from [button check] |
|---|---|---|---|---|
| 57 | SudeTachiKamaeAttackModori | gym_H_CH_NN_FTtoFN_nakayama<br>gym_H_FN_NN_Breath_lp_nakayama (Motion_SudeTachiKamae) | - | - |
| 251 | Sliding | gym_testyarare2_BD_ | 8P f20-50 | - |
| 252 | SlidingAttack2 | gym_HE_FT_NN_BD_slidingUpperR_NT | 25P f32-44, 25P f32-52 | Sliding<br>Sliding [pressed 0x800] |
| 253 | HandGun_DownAttack1 | gym_H_FT_NN_BD_F1_downk1_YS | 10P f20-36, 10P f22-36 | SUB WeponAttackSub [pressed 0x200] |
| 254 | HandGun_Attack2 | gym_HE_FT_NN_BD_HookL_NT | 7P f30-36 | SUB WeponAttackSub [pressed 0x200] |
| 255 | HandGun_Attack2Attack2 | gym_HE_FT_NN_BD_UrakenL_NT | 10P f26-32 | HandGun_Attack2 [pressed 0x800] |
| 256 | HandGun_BMoveAttack2 | gym_testyarare4_BD_ | 15P f36-46, 18P f36-46, 22.5P f36-46, 30P f36-46, 45P f36-46 | SUB WeponAttackSub [pressed 0x200] |
| 257 | HandGun_Attack1 | gym_testyarare3_BD_ | 10P f24-34, 15P f24-34, 20P f24-34, 30P f24-34, 40P f24-34 | - |
| 258 | HandGun_Attack1_kawarawari | gym_testyarare3_BD_ | 16P f22-36, 16P f24-36 | SudeTachiRMove1Attack2Attack2Attack2Attack2Attack2 [pressed 0x200] |
| 259 | HandGun_BMoveAttack1 | gym_HE_FT_NN_BD_RtukiK_NT | 15P f26-34 | - |
| 260 | HandGun_Attack1_combo1 | gym_testyarare3_BD_ | 10P f26-34 | - |
| 261 | Rocket_Attack1 | gym_HE_FT_NN_BD_HighKickL_NT | 13P f26-34 | SUB WeponAttackSub [pressed 0x200] |
| 264 | GuardAttack1 | gym_testyarare1_BD_ | 10P f14-32, 5 f14-32, 30P f14-32, 20P f14-32 | SudeGuardT0L1 [pressed 0x200]<br>SudeGuardT0L2 [pressed 0x200]<br>SudeGuardT0L3 [pressed 0x200]<br>SudeGuardT0L4 [pressed 0x200]<br>YarareCancel_SudeGuardT0L1 [pressed 0x200] |
| 265 | GuardAttack2_st | gym_HE_FN_NN_BD_guardend_SW<br>gym_H_FN_NN_Breath_lp_nakayama | - | SudeGuardT0L1 [pressed 0x800]<br>SudeGuardT0L2 [pressed 0x800]<br>SudeGuardT0L3 [pressed 0x800]<br>SudeGuardT0L4 [pressed 0x800]<br>YarareCancel_SudeGuardT0L1 [pressed 0x800] |
| 266 | GuardAttack2 | gym_HE_FT_NN_BD_GreatRstr_NT | 5 f27-42, 5 f24-42, 10P f24-42 | GuardAttack2_st |
| 267 | SlidingAttack1 | gym_testyarare1_BD_ | 18P f14-32 | Sliding [pressed 0x200]<br>Sliding [pressed 0x800] |
| 268 | SlidingGunAttack1 | gym_testyarare1_BD_ | 10P f14-32 | HandGun_Attack1 [held 0x800]<br>SudeTachiLMove1Attack1 [held 0x800] |
| 276 | SudeTachiAttack1 | gym_HE_FT_NN_BD_Rstr_NT | 8P f22-28 | SUB AttackCansel<br>SUB TachiAS [held 0x4000000]<br>SUB nomal_attack [pressed 0x800] |
| 277 | SudeTachiAttack2Attack2Attackslow1 | gym_HE_FT_NN_BD_Rstr_NT | 8P f22-28 | SudeTachiAttack2Attack2 |
| 278 | SudeTachiAttack1Attackfast1 | gym_HE_FN_NN_BD_up_SW | 10P f27-34 | SudeTachiAttack1 [held 0x800] |
| 279 | SudeTachiAttack1Attackfast1Attack2 | gym_testyarare5_BD_ | 18P f34-44 | DownAttack1<br>DownAttack1 [pressed 0x200]<br>SudeTachiAttack1Attackfast1 |
| 280 | SudeTachiAttack1Attack1 | gym_HE_FT_NN_BD_RtukiK_NT | 10P f28-34 | SudeTachiAttack1 [held 0x800] |
| 281 | SudeTachiAttack2Attack1 | gym_HE_FT_NN_BD_Rstr_NT | 8P f22-28 | SudeTachiAttack2 [pressed 0x800] |
| 282 | SudeTachiAttack2Attack1Attack2 | gym_HE_FN_NN_BD_upL_SW | 12P f38-44, 15.6P f38-44, 16.8P f38-44, 24P f38-44 | SudeTachiAttack2Attack1 [pressed 0x800] |
| 283 | SudeTachiAttack2Attack1Attack1 | gym_HE_FT_NN_BD_RtukiK_NT | 18P f28-34 | SudeTachiAttack2Attack1 [pressed 0x800] |
| 284 | SudeTachiAttack2 | gym_HE_FT_NN_BD_Ljab_NT | 4P f18-24 | SUB AttackCansel<br>SUB nomal_attack [pressed 0x200] |
| 285 | SudeTachiAttack2Attack2Attack1 | gym_HE_FT_NN_BD_GreatRstr_NT | 20P f24-42 | HandGun_Attack1_kawarawari<br>SlidingGunAttack1<br>SudeTachiAttack2Attack2 |
| 286 | SudeTachiAttack2Attack1Attack2Attack1_start | gym_H_FT_NN_BD_PO_genkidama_YS | - | SudeTachiAttack2Attack1Attack2 [held 0x200] |
| 287 | SudeTachiAttack2Attack1Attack2Attack1 | gym_HE_FT_NN_BD_GreatRstr_NT | 40P f24-42 | SudeTachiAttack2Attack1Attack2Attack1_start |
| 288 | SudeTachiAttack2Attack2Attack2 | id 1263 (no animation with this id) | 10P f60-65 | - |
| 289 | SudeTachiFMove1Attack1 | gym_HE_FT_NN_BD_JumpUpperR_NT | 25P f32-44, 25P f32-52 | SUB AttackCansel<br>SUB TachiAS [pressed 0x800]<br>SlidingGunAttack1 |
| 290 | SudeTachiBMove1Attack1 | gym_HE_FT_NN_BD_RtukiK_NT | 10P f28-34, 12P f28-34, 13P f28-34 | SUB AttackCansel<br>SUB TachiAS [pressed 0x10000000] |
| 291 | SudeTachiLMove1Attack1 | gym_HE_FN_NN_BD_up_SW | 12P f27-34, 12P f22-34 | SUB AttackCansel<br>SUB TachiAS [pressed 0x800]<br>SudeTachiAttack2Attack2 |
| 292 | SudeTachiRMove1Attack1 | gym_HE_FT_NN_BD_RHookJ_NT | 8P f19-28 | SUB AttackCansel<br>SUB TachiAS [pressed 0x800] |
| 293 | SudeTachiFMove1Attack2 | gym_HE_FT_NN_BD_keriageL_NT | 48P f48-56 | SUB AttackCansel<br>SUB TachiAS [pressed 0x200] |
| 294 | SudeTachiBMove1Attack2 | gym_HE_FT_NN_BD_HighKickL_NT | 16P f27-34 | SUB AttackCansel<br>SUB TachiAS [pressed 0x800] |
| 295 | SudeTachiLMove1Attack2 | gym_HE_FT_NN_BD_HookL_NT | 9P f28-34 | SUB AttackCansel<br>SUB TachiAS [pressed 0x200] |
| 296 | SudeTachicombo1_Attack1 | gym_HE_FT_NN_BD_BmrHookR_NT | 14P f26-32 | SudeTachiLMove1Attack2<br>SudeTachicombo1_Attack2 |
| 297 | SudeTachicombo2_Attack1 | gym_HE_FT_NN_BD_JumpUpperR_NT | 18P f32-44 | SudeTachicombo1_Attack2<br>SudeTachicombo1_Attack2 [pressed 0x800] |
| 298 | SudeTachicombo1_Attack2 | gym_HE_FT_NN_BD_HookL_NT | 14P f28-34 | SudeTachiRMove1Attack1<br>SudeTachicombo1_Attack1 |
| 299 | SudeTachicombo2_Attack2 | gym_HE_FT_NN_BD_chop_SW | 12P f20-28, 12P f21-25 | HandGun_Attack1_kawarawari<br>SudeTachicombo1_Attack1<br>SudeTachicombo1_Attack1 [pressed 0x200] |
| 300 | SudeTachiRMove1Attack2 | gym_HE_FT_NN_BD_UrakenL_NT | 8P f26-32 | SUB AttackCansel<br>SUB TachiAS [pressed 0x200] |
| 301 | SudeTachiRMove1Attack2Attack2 | gym_HE_FT_NN_BD_UrakenL_NT | 2P f26-32 | SudeTachiRMove1Attack2 [pressed 0x200] |
| 302 | SudeTachiRMove1Attack2Attack2Attack2 | gym_HE_FT_NN_BD_UrakenL_NT | 2P f26-32 | SudeTachiRMove1Attack2Attack2 [pressed 0x200] |
| 303 | SudeTachiRMove1Attack2Attack2Attack2Attack2 | gym_HE_FT_NN_BD_UrakenL_NT | 2P f26-32 | SudeTachiRMove1Attack2Attack2Attack2 [pressed 0x200] |
| 304 | SudeTachiRMove1Attack2Attack2Attack2Attack2Attack2 | gym_HE_FT_NN_BD_UrakenL_NT | 2P f26-32 | SudeTachiRMove1Attack2Attack2Attack2Attack2 [pressed 0x200] |
| 305 | SudeTachiRMove1Attack2Attack2Attack2Attack2Attack2Attack2 | gym_HE_FT_NN_BD_UrakenL_NT | 12P f26-36 | SudeTachiRMove1Attack2Attack2 [pressed 0x200]<br>SudeTachiRMove1Attack2Attack2Attack2Attack2 [pressed 0x200]<br>SudeTachiRMove1Attack2Attack2Attack2Attack2Attack2 [pressed 0x800] |
| 306 | SudeTachiAttack2PW | gym_H_FT_NN_BD_PO_genkidama_YS | - | SudeTachiAttack1 [held 0x800] |
| 307 | SudeTachiAttack1andAttack2 | gym_HE_FT_NN_BD_bomber_SW | 6P f8-38 | SUB nomal_attack [pressed 0x200]<br>SudeTachiAttack1 [held 0x200]<br>SudeTachiAttack2 [held 0x800] |
| 331 | SudeTachiLRotate1Attack1 | id 79 (no animation with this id) | - | - |
| 332 | SudeTachiLRotate1Attack2 | id 79 (no animation with this id) | - | - |
| 333 | SudeTachiRRotate1Attack1 | id 79 (no animation with this id) | - | - |
| 334 | SudeTachiRRotate1Attack2 | id 79 (no animation with this id) | - | - |
| 335 | SudeSyagamiAttack1 | gym_HE_NN_MM_BD_shagaP2_SW<br>gym_H_FN_MM_Breath_lp_nakayama (Motion_SudeSyagamiKamae) | 2P f20-29 | SUB SyagamiAS [pressed 0x800] |
| 336 | SudeSyagamiAttack1Attack1 | id 78 (no animation with this id) | - | - |
| 337 | SudeSyagamiAttack1Attack1Attack1 | id 79 (no animation with this id) | - | SudeSyagamiAttack1Attack1 |
| 338 | SudeSyagamiAttack2 | gym_HE_NN_MM_BD_shagaP_SW | 2P f13-20 | SUB SyagamiAS [pressed 0x200] |
| 339 | SudeSyagamiLRotate1Attack1 | gym_HE_NN_MM_BD_shagaP_SW | 8 f13-17 | - |
| 340 | SudeSyagamiLRotate1Attack2 | id 72 (no animation with this id) | 8 f13-17 | - |
| 341 | SudeSyagamiRRotate1Attack1 | gym_HE_NN_MM_BD_shagaP_SW | 8 f13-17 | - |
| 342 | SudeSyagamiRRotate1Attack2 | id 72 (no animation with this id) | 8 f13-17 | - |
| 343 | SudeSyagamiFMove1Attack1 | gym_HE_NN_MM_BD_shagaP_SW | 8 f13-17 | - |
| 344 | SudeSyagamiFMove2Attack1 | gym_HE_NN_MM_BD_shagaP_SW | 8 f13-17 | - |
| 345 | SudeSyagamiFMove1Attack2 | id 72 (no animation with this id) | 8 f13-17 | - |
| 346 | SudeSyagamiFMove2Attack2 | id 72 (no animation with this id) | 8 f13-17 | - |
| 347 | SudeSyagamiBMove1Attack1 | gym_HE_NN_MM_BD_shagaP_SW | 8 f13-17 | - |
| 348 | SudeSyagamiBMove1Attack2 | id 72 (no animation with this id) | 8 f13-17 | - |
| 349 | SudeSyagamiLMove1Attack1 | gym_HE_NN_MM_BD_shagaP_SW | 8 f13-17 | - |
| 350 | SudeSyagamiLMove1Attack2 | id 72 (no animation with this id) | 8 f13-17 | - |
| 351 | SudeSyagamiRMove1Attack1 | gym_HE_NN_MM_BD_shagaP_SW | 8 f13-17 | - |
| 352 | SudeSyagamiRMove1Attack2 | id 72 (no animation with this id) | 8 f13-17 | - |
| 353 | SudeHuseAttack1 | id 71 (no animation with this id) | - | - |
| 354 | SudeHuseAttack1Attack1 | id 78 (no animation with this id) | - | SudeHuseAttack1 |
| 355 | SudeHuseAttack1Attack1Attack1 | id 79 (no animation with this id) | - | SudeHuseAttack1Attack1 |
| 356 | SudeHuseAttack2 | id 72 (no animation with this id) | - | - |
| 357 | SudeHuseLRotate1Attack1 | id 79 (no animation with this id) | - | - |
| 358 | SudeHuseLRotate1Attack2 | id 79 (no animation with this id) | - | - |
| 359 | SudeHuseRRotate1Attack1 | id 79 (no animation with this id) | - | - |
| 360 | SudeHuseRRotate1Attack2 | id 79 (no animation with this id) | - | - |
| 361 | SudeHuseFMove1Attack1 | id 79 (no animation with this id) | - | - |
| 362 | SudeHuseFMove2Attack1 | id 79 (no animation with this id) | - | - |
| 363 | SudeHuseFMove1Attack2 | id 84 (no animation with this id) | - | - |
| 364 | SudeHuseFMove2Attack2 | id 84 (no animation with this id) | - | - |
| 365 | SudeHuseBMove1Attack1 | id 84 (no animation with this id) | - | - |
| 366 | SudeHuseBMove1Attack2 | id 84 (no animation with this id) | - | - |
| 367 | SudeHuseLMove1Attack1 | id 84 (no animation with this id) | - | - |
| 368 | SudeHuseLMove1Attack2 | id 84 (no animation with this id) | - | - |
| 369 | SudeHuseRMove1Attack1 | id 84 (no animation with this id) | - | - |
| 370 | SudeHuseRMove1Attack2 | id 84 (no animation with this id) | - | - |
| 371 | KucyuuAttack1 | gym_H_FN_NN_BD_riderkick_SW | 17P f32-54, 17P f30-54, 22.1P f29-54, 27.2P f29-54, 37.4P f29-54, 51P f29-54, 153 f29-54, 68P f29-54, 204 f29-54 | DownAttack1 [pressed 0x200]<br>SUB TachiAS [pressed 0x800] |
| 372 | KucyuuAttack2 | id 5814 (no animation with this id) | 12P f46-55 | - |
| 373 | DownAttack1 | gym_H_FT_NN_BD_F1_downk1_YS | 10P f20-36, 10P f22-36 | SUB nomal_attack [pressed 0x200]<br>SudeTachiAttack1 [held 0x800] |
| 391 | soulus_SudeTachiAttackPW | gym_BN_FT_NN_BD_eneball1_SR | - | SUB nomal_attack [pressed 0x200]<br>Solus_SudeTachiAttack1Attack1Attack2 |
| 392 | soulus_SudeTachiFMoveAttackPW | gym_BN_FT_NN_BD_eneball2_SR | - | BURNS_combo_backstep<br>SUB TachiAS [pressed 0x200] |
| 393 | soulus_SudeTachiBMoveAttackPW | gym_BN_FT_NN_BD_eneball3_SR | - | SUB TachiAS [pressed 0x800] |
| 412 | NageY_L_Com | - | - | SUB NageY_Check_L |
| 413 | NageY_L_Done | - | - | NageY_L_Com |
| 414 | NageY_R_Com | - | - | SUB NageY_Check_R |
| 415 | NageY_R_Done | - | - | NageY_R_Com |
| 435 | SudeTachiAttack2Attack2 | gym_HE_FT_NN_BD_Ljab_NT | 4P f18-24 | SudeTachiAttack2 [pressed 0x200] |
| 439 | NageN_Com2 | - | - | - |
| 440 | NageN_Done2 | gym_HE_FN_NN_BD_test_SW | - | NageN_Com2<br>SudeTachi_nage_jyunbi |
| 441 | NageN_Done2_B | gym_HE_FN_NN_BD_test2_SW | - | NageN_Com2<br>SudeTachi_nage_jyunbi |
| 442 | SudeTachi_nage_jyunbi | gym_testyarare0_BD_ | - | SUB TachiAS [held 0x200]<br>SUB TachiAS [pressed 0x200]<br>SUB TachiAS [pressed 0x800]<br>SudeTachiAttack1andAttack2<br>SudeTachiBMove1Attack1 [held 0x200]<br>SudeTachiBMove1Attack2 [held 0x800] ... |
| 503 | NageN_Com | - | - | SUB NageN_Check<br>SUB SudeSyagamiIC<br>SUB SudeTachiIC |
| 504 | NageN_L_Done | gym_s07d04_030_BN_BD_ReadyL_SW | - | NageN_Com |
| 505 | NageN_R_Done | gym_s07d04_030_BN_BD_Ready_SW | - | NageN_Com |
| 517 | NageY_Com2 | - | - | SUB SudeTachiIC |
| 518 | NageY_Done2 | - | - | NageY_Com2 |
| 519 | NageY_Com2_B | - | - | SUB SudeTachiIC |
| 520 | NageY_Done2_B | - | - | NageY_Com2_B |
