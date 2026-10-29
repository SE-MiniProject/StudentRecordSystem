# Software Engineering Course — Mini Project

This repository contains the source code, documentation, and other project artifacts for our **Software Engineering course Mini Project**.

**Student Record System**

A simple system to keep all student records in one place: student details, attendance, grades, and assignments.

This is our Software Engineering mini project. There are 4 of us on the team.

**What is this?**

Schools usually have student info scattered around. This project puts it all in one database, and each person only sees what they're supposed to see.

There are 3 types of users:

**Admin** looks after the main records
**Teacher** handles attendance, grades, and assignments
**Student** checks their own records and submits assignments


**Admin**

Add, edit, and delete students, teachers, classes, and subjects
Upload student data from a file
Has access to all the main records

**Teacher**

See the students in their own classes
Mark and update attendance
Add and update grades
Create and manage assignments

**Student**

See their own attendance
See their own grades
See assignments
Submit assignments

**Parts of the system**

Student Record System
 Login
 Admin(
   Students
   Teachers
   Classes
   Subjects)
 Teacher(
    Attendance
    Grades
    Assignments)
 Student(
    Attendance
    Grades
    Assignments
    Submit Assignment)
 Database

## Repository layout

```
client/     Qt Widgets desktop app (CMake, Qt 6, C++17)
server/     C++ REST server (CMake, C++20)
db/         PostgreSQL schema — pure SQL
  migrations/  numbered, applied in order
  seeds/       reference_dataset.sql (benchmark set) · demo_data.sql (small demo set)
  checks/      sanity.sql
docs/       SRS, SAD, test plan, sprint plan, diagrams, templates
```

Inside `client/` and `server/`, the module folders (`login/`, `auth/`, `students/`, …) are created together with their first real source file — nothing empty is committed beyond the top-level lanes.

## Project Management

We use **Jira** to manage the project's tasks, track progress, and coordinate development.

**Jira Project:**
https://stu-team-ls27bn2k.atlassian.net/jira/software/projects/SRS/summary?atlOrigin=eyJpIjoiMjNlYWM3MWY3NGM1NGU5YjgxYjRkYTI5OGQ5M2YxOGIiLCJwIjoiaiJ9


