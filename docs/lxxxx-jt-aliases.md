# lXXXX ↔ JT-export alias map

Auto-generated from `data/work/disasm/CODE_*.s` (every `entry_jtNNN:` immediately
followed by an `LXXXX:` is that JT export's local address).
Regenerate with `tools/gen_jt_aliases.sh > docs/lxxxx-jt-aliases.md`.

## WHY THIS EXISTS (read before lifting any lXXXX)

A CODE-local `lXXXX` that is ALSO a `JT[N]` export is the SAME function and is
very likely ALREADY lifted as `jtN` elsewhere in boot.c.  Before lifting an
`lXXXX` arm/helper: (1) look it up here; (2) if it maps to a `jtN`, grep boot.c
for that `jtN` — if it has a real body, ALIAS/repoint to it, don't re-lift (the
l30bc=jt882, l25ce=jt893, l2f6e duplicate traps).  NOTE: the same hex offset
recurs across CODE segments (l3540 / l2f6e / l23d2 exist in several) — these are
DIFFERENT functions; match on (CODE, offset).  When a needed name collides with
an already-lifted other-segment lXXXX, suffix `_cNN` (e.g. l23d2_c19, l3540_c19).

## Map (by CODE segment)

### CODE 03

l0644 = jt464  l0846 = jt458  l0b7a = jt465  l0c0a = jt460  l0d1a = jt468  l0d44 = jt459  l148a = jt448  l1676 = jt443  l20c4 = jt381  l224c = jt380  l2438 = jt379  l25a2 = jt378  l2c60 = jt449  l2d3e = jt456  l30ba = jt446  l3198 = jt440  l31ea = jt441  l31f0 = jt439  l32e2 = jt418  l366a = jt406  l36bc = jt387  l36ec = jt385  l3738 = jt389  l37da = jt415  l384a = jt410  l3888 = jt412  l3952 = jt384  l39ae = jt423  l39d2 = jt399  l39f0 = jt417  l3b2c = jt413  l3b4e = jt397  l3bda = jt396  l3c34 = jt407  l3d98 = jt414  l3de2 = jt411  l3e0c = jt409  l3e3c = jt390  l3fb8 = jt400  l45d6 = jt402  l4648 = jt420  l466a = jt408  l46b2 = jt395  l4762 = jt386  l4796 = jt394  l49a2 = jt433  l4b8e = jt431  
### CODE 04

l04cc = jt1166  l04de = jt1179  l04f0 = jt1200  l053e = jt1177  l1468 = jt1186  l16e0 = jt1193  l17ee = jt1196  l1952 = jt1168  l1aa0 = jt1161  l212c = jt1203  l24aa = jt1178  l3e38 = jt1162  l4350 = jt1159  l4d88 = jt1124  l5104 = jt1128  l5c82 = jt1146  l5d8c = jt1116  l6204 = jt1113  l6710 = jt1118  l73ee = jt1091  l765c = jt1151  l7690 = jt1122  l77fe = jt1135  l78e8 = jt1141  l79c2 = jt1149  
### CODE 05

l0062 = jt1081  l0088 = jt1085  l00a8 = jt1088  l00da = jt1090  l0156 = jt1080  l01ae = jt1083  l0334 = jt1089  l036a = jt1084  l0440 = jt1078  l0be4 = jt1008  l0f1e = jt984  l0f48 = jt979  l0f9c = jt980  l0faa = jt982  l12b4 = jt985  l17e2 = jt988  l1a0c = jt987  l2850 = jt1004  l2856 = jt1003  l289a = jt998  l309c = jt999  l330c = jt1007  l3640 = jt1016  l36a4 = jt1014  l3736 = jt1018  l37aa = jt1012  l3834 = jt1015  l3a0e = jt1011  l4010 = jt973  l46a6 = jt1022  l4a74 = jt1065  l550c = jt1038  l5716 = jt1044  l59ee = jt1050  l71b0 = jt1069  l7a0e = jt1073  l7ab4 = jt1076  l7c74 = jt1072  
### CODE 06

l035e = jt131  l0dc6 = jt28  l11a0 = jt34  l1238 = jt32  l14fc = jt13  l1554 = jt37  l1c92 = jt27  l1f3e = jt16  l2000 = jt24  l22da = jt18  l241e = jt20  l2456 = jt25  l2526 = jt41  l2cb0 = jt19  l2f4c = jt29  l2f74 = jt35  l2fd8 = jt40  l3038 = jt26  l31dc = jt115  l338c = jt113  l33ac = jt110  l37d6 = jt118  l3804 = jt114  l3828 = jt123  l3880 = jt106  l38d0 = jt108  l3918 = jt120  l3994 = jt117  l3b1e = jt111  l3eea = jt124  l3f3c = jt105  l3fd6 = jt94  l42a0 = jt97  l43c4 = jt96  l4b40 = jt101  l4b84 = jt99  l4bac = jt92  l4bf6 = jt103  l5124 = jt88  l534a = jt46  l541a = jt47  l5700 = jt45  l579e = jt43  l5822 = jt44  l5864 = jt48  l5888 = jt52  l5ac2 = jt50  l5ad8 = jt51  l5b5e = jt55  l5f3a = jt64  l5f4e = jt65  l5f66 = jt69  l5f84 = jt60  l6048 = jt66  l604e = jt68  l6096 = jt62  l60b4 = jt63  l6114 = jt73  l61c6 = jt71  l670c = jt76  l67ca = jt78  l68ae = jt80  l68f8 = jt84  l6ada = jt85  
### CODE 07

l11a8 = jt155  l11ee = jt179  l159a = jt153  l15a8 = jt162  l15ae = jt170  l15bc = jt177  l162e = jt176  l16ea = jt159  l17f8 = jt175  l1806 = jt181  l1f3e = jt158  l2062 = jt174  l2858 = jt166  l2ebc = jt160  l34f0 = jt182  l3564 = jt143  l35e6 = jt168  l3600 = jt169  l38e4 = jt157  l38f8 = jt180  l3aba = jt186  l483e = jt184  l4910 = jt187  l4aee = jt196  l4c88 = jt191  l4db4 = jt195  l4e3a = jt192  l4fbe = jt193  l52b8 = jt218  l5484 = jt207  l5752 = jt216  l57f2 = jt219  l59d4 = jt200  l5bfa = jt210  l5e52 = jt202  l6148 = jt203  l6234 = jt199  l6ea2 = jt220  
### CODE 08

l0004 = jt373  l0de4 = jt327  l16a8 = jt328  l324c = jt330  l33f6 = jt331  l3658 = jt333  l3f2e = jt334  l41de = jt337  l45c6 = jt336  l4a16 = jt332  l567c = jt342  l5efe = jt356  l5f04 = jt363  l600c = jt362  l6520 = jt368  l6594 = jt366  l6e50 = jt364  l6ed2 = jt370  l6f9e = jt346  l7222 = jt369  l7404 = jt360  
### CODE 09

l0e2c = jt323  l224a = jt324  l3632 = jt322  
### CODE 10

l62c6 = jt261  l65be = jt265  
### CODE 12

l0082 = jt936  l02dc = jt937  l0848 = jt934  l185e = jt917  l1baa = jt923  l1c8a = jt925  l1d90 = jt921  l229e = jt924  l2504 = jt926  l2554 = jt922  l2cf4 = jt927  l3b4a = jt929  l45ca = jt932  
### CODE 13

l1888 = jt495  l18de = jt497  l2406 = jt493  l242c = jt490  l2484 = jt492  l25f4 = jt494  l26ea = jt498  l276c = jt496  l279c = jt499  l27e6 = jt504  l2bde = jt491  l6eba = jt509  l6f68 = jt507  l713c = jt506  l72d0 = jt505  l76da = jt508  
### CODE 14

l0f60 = jt543  l10c4 = jt554  l13e6 = jt540  l1956 = jt550  l19dc = jt555  l2d48 = jt544  l2dbc = jt533  l3b6c = jt539  l4186 = jt546  l5d92 = jt517  l6090 = jt518  l61ae = jt524  l62ec = jt528  l635e = jt532  l6520 = jt514  l6554 = jt516  l6836 = jt521  l6a10 = jt523  l6b40 = jt525  l6b6a = jt531  l6b94 = jt513  l6bbe = jt519  l6c22 = jt515  l6de8 = jt520  l73cc = jt530  l7488 = jt522  l7894 = jt529  
### CODE 15

l03d2 = jt593  l03fe = jt577  l0934 = jt578  l0ce0 = jt591  l0d9c = jt588  l1b74 = jt590  l1c92 = jt583  l1cd2 = jt586  
### CODE 16

l16de = jt607  l19c8 = jt631  l46ca = jt638  l48f4 = jt670  l59c2 = jt597  l6af8 = jt596  l73ea = jt598  
### CODE 17

l1346 = jt573  l3cd4 = jt575  l4df0 = jt559  l618c = jt560  l6bee = jt558  l6cd2 = jt557  
### CODE 18

l0006 = jt860  l009e = jt878  l0420 = jt868  l1532 = jt866  l15f4 = jt870  l1638 = jt873  l1666 = jt876  l17b6 = jt880  l17ec = jt865  l1b14 = jt875  l2018 = jt867  l2308 = jt871  l23e6 = jt869  l24f8 = jt874  l3e5c = jt855  l77a0 = jt857  
### CODE 19

l035c = jt914  l0528 = jt913  l1276 = jt886  l19ac = jt898  l1abe = jt892  l1f00 = jt895  l25ce = jt893  l2d78 = jt890  l2f6e = jt902  l30bc = jt882  l3540 = jt903  l35a0 = jt889  l3f16 = jt884  l3fd2 = jt891  l420e = jt897  l422a = jt901  l4248 = jt883  l46e0 = jt894  l4c9a = jt887  l5274 = jt899  l58d4 = jt881  l596a = jt888  l5ba0 = jt911  l5d8a = jt912  l5df2 = jt908  l668e = jt907  l687e = jt906  
### CODE 20

l0b20 = jt950  l0b88 = jt951  l0ba2 = jt952  l4108 = jt941  l41fa = jt944  l4218 = jt939  l4256 = jt946  l427c = jt940  l472a = jt942  l4738 = jt943  l694e = jt945  l709e = jt947  
### CODE 21

l03b2 = jt958  l0798 = jt961  l0aba = jt959  l1a34 = jt960  l38f4 = jt954  
### CODE 22

l0476 = jt321  l04d6 = jt310  l0524 = jt285  l056c = jt277  l05ca = jt293  l0614 = jt283  l0674 = jt300  l069a = jt288  l0716 = jt306  l073e = jt298  l07be = jt305  l1798 = jt299  l17ca = jt304  l1a6e = jt311  l2180 = jt303  l22c4 = jt308  l23ee = jt312  l265e = jt280  l294e = jt278  l2aaa = jt286  l2f24 = jt282  l329c = jt281  l3792 = jt296  l3998 = jt295  l423e = jt279  l475e = jt276  l48b2 = jt301  l48ca = jt291  l48e4 = jt307  l4900 = jt273  l494e = jt314  
