# minictl

minictl, the minichord from the command line, lives at https://github.com/keyandcableco/minictl and works with stock firmware and this fork's:

```
pip install "minichord[all] @ git+https://github.com/keyandcableco/minictl"
```

It carries a copy of `firmware/preset_maker` (the describer) and of this fork's `generator/parameters.json`. After changing either, refresh them there with `python sync.py --fork <this checkout>`.
