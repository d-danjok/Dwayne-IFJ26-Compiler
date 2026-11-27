# Git

## Commits

* Commit should be done for every functional change or major refactoring, not half-way of change and not on more than one change

* Commit format:
  `<type>(<scope>): <short description>` where:
  
    | Type       | When to use                               |
    |------------|-------------------------------------------|
    | `feat`     | New feature                               |
    | `fix`      | Bug fix                                   |
    | `docs`     | Documentation only                        |
    | `style`    | Formatting, no code logic change          |
    | `refactor` | Code restructuring, no new features/fixes |
    | `test`     | Adding or updating tests                  |
    | `chore`    | Build config, packages, tooling           |

## Branching

* One major feature - one branch

* For fixing smth   - separate branch

* No commits to the main branch

* Naming format:
  `<branch creator's xlogin>/<type (same as for commmit)>/<name>`
* Every PR should be reviewed by other members of the team, ideally acceptance tests should be created on separate branch before opening PR

## How to work on dev branches

Dev branches are meant for merging more branches together for integration, so they require work of more than 1 people on the same time

* Pull request for every change, do not commit directly to branch

* Naming format:
  `<dev branch name>/<type (same as for commmit)>/<name>`

# ! MORE THAN 1 PERSON SHOULD NEVER WORK ON SINGLE BRANCH 