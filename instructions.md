VHS by Memorecks (James Peck) is an audio effect that emulates the sound of VHS tape. It takes incoming audio and processes it with an effects chain to modify and degrade the signal. It was originally built in Reaktor 6 (Native Inctruments) and typically works inside of the Reaktor 6 plugin. Today, we will build a standalone VST/AU port of this effect. I have provided a description and overview of the signal chain, a screenshot of the original UI in Reaktor, and the Reaktor 6 ensemble itself (.ens) file. These are located in the /resources directory.

Here are some general guidelines:
- Try to emulate the effects chain accuratley
- Build a Mac (x86+ARM) comptable VST3 and AU plugin
- VST3 plugin should also work on windows
- Feel free to improve on the UI design, but keep the general spirit and layout of the reference

The provided .ens / Reaktor file would be the source of truth. Let's do our best to see what data we can gleam, as it may be a binary format. Also feel free to poke around in my Reaktor 6 app/plugin files, but please don't modify anything there directly.