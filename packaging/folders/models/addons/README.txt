Chaos Rings III (CR3-U): add-ons - outfits and weapons of your own.

Each is a manifest, <anything>.ini, and its glTF model (.glb), here. The model is made from an
export (cr3view --export-models): an outfit over a character's body (j001b02.glb), a weapon over a
weapon (w001_01.glb), keeping the armature (bones node_<n>...) and material names (cr_mat_<n>...).

  id = 1               1 .. 999 - keep it: saves know what you own by it
  kind = outfit        or weapon
  character = 1        who wears it (1 the hero)
  name = Night Coat    its name (name_ja = for Japanese)
  model = coat.glb     beside the manifest
  like = 200083        the item it is made from: its numbers, icon, body or weapon (default: the character's own)
  price = 30           in the premium shop (shop = none: not sold)
  currency = coin      coin, GP or gold
  hd_models = no       an outfit: yes - its own model while HD Models replaces the character; or the replaced figures it fits (j001b01 j001h01)

They're sold in the premium shop (the field's Shop button; the VS mode's Sp Shop) and worn like the
game's. A weapon is always drawn as its own model. While HD Models replaces one of the character's
figures, an outfit that doesn't say it fits looks like the item it's made from (its numbers its own). cr3tool addons <release> <this folder> checks them.
Personal use only: they are made from the game's models. The README's "Add-ons".
