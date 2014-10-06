paktools
========

WayForward Engine resource packer for https://github.com/artlavrov/adventuretime-rus

* MultiArc Plugin support!

MultiArc custom.ini settings:

```
[WayForward]
TypeName=WayForward
ID=46 49 4C 45 4C 49 4E 4B
IDPos=8
Extension=pak
List=paktools.exe -l %%A
Extract=paktools.exe %%A %%W %%F
ExtractWithoutPath=paktools.exe -p %%A %%W %%F
Test=paktools.exe %%A %%W %%f
Format0=".......... zzzzzzzzzz nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn"
Errorlevel=1
```