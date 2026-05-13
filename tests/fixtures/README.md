# Test Fixtures

Fixture tests are driven by `manifest.txt` plus an optional machine-local
`local_manifest.txt`.

Fixture files are not downloaded by the test runner. Download public fixtures
into this directory, or add private fixtures with paths relative to
`local_manifest.txt`.

The tracked public manifest currently includes:

- `testslide.isyntax` from `https://zenodo.org/record/5037046/files/testslide.isyntax`

Additional public WSI files can be added from sources such as
`https://downloads.openmicroscopy.org/images/` when they are useful for a
specific format-reader regression.

For local private fixtures, copy `local_manifest.example.txt` to
`local_manifest.txt` and edit the paths if needed.
