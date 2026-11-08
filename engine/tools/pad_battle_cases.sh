# Sourced by pad_vita_test.sh; run the actual viewer with its SDL virtual controller.
want battle && run battle-command-cursor \
    "0.1:until=fighter_0_chooses 0.5:down 1:down 1.5:up 2:south 3:quit" \
    "fighter 0 chooses|battle: cursor 1|battle: cursor 2|battle: cursor 1" \
    -- --battle 2612 --team-level 30 --input "battle"

# The item list's two native categories include gifts; L and R browse both directions.
want battle && run battle-item-categories \
    "0.1:until=items: 0.5:r 1:l 1.5:south 2:quit" \
    "items:|battle: item category 1|battle: item category 0" \
    -- --battle 2612 --team-level 30 --item 101:3 --input "battle item"

# Unlike button menus, comListMenu stops at both ends instead of wrapping.
want battle && run battle-list-boundaries \
    "0.1:until=items: 0.4:up 0.8:down 1.2:down 1.6:up 2:quit" \
    "test controller: up|test controller: down|battle: cursor 1|test controller: down|test controller: up|battle: cursor 0" \
    -- --battle 2612 --team-level 30 --item 101:3 --item 102:3 --input "battle item"

# The first confirm during the result tally completes it; the second dismisses it.
want battle && run battle-victory-confirm \
    "0.1:until=victory_camera_ready 0.2:east 1:quit" \
    "battle: victory camera ready|battle: victory camera skipped|result -" \
    -- --battle 2612 --team-level 30 --input "win"

want battle && run battle-result-confirm \
    "0.1:until=result_- 0.1:east 0.5:east 1.5:quit" \
    "result -|battle: result tally completed" \
    -- --battle 2612 --team-level 30 --input "win"
