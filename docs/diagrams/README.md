# Diagrams

Graphviz sources (`*.dot`) for the flowcharts in the docs, and their rendered SVGs (committed, so GitHub and the docs site show them without Graphviz installed).

After changing a `.dot` file: `make diagrams` (needs `dot`: `apt install graphviz`).

The shared look, repeated at the top of each file (Graphviz has no includes):

- rounded boxes: steps; diamonds: decisions; ovals: where it starts and ends;
- grey notes: what the step is in the code;
- a dashed edge: work handed to another task (it comes back by a poll).
